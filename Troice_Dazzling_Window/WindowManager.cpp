#include "WindowManager.h"
#include "main.h"
#include <filesystem>
#include <unordered_map>


// 全局变量
static std::unique_ptr<asio::io_context> g_io_context;
static std::unique_ptr<TcpServer> g_server;
static std::unique_ptr<std::thread> g_io_thread;
static bool g_serverRunning = false;

// 服务器配置
static char serverIP[256] = "127.0.0.1";     // IP地址输入缓冲区
static int serverPort = 12345;               // 端口号
static char serverName[256] = "";            // 服务器名称


void ConvertAndShowMessage(const std::string& cmdContent) 
{
	int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdContent.c_str(), -1, NULL, 0);
	if (wlen > 0) {
		std::wstring wstr(wlen, 0);
		MultiByteToWideChar(CP_UTF8, 0, cmdContent.c_str(), -1, &wstr[0], wlen);
		MessageBoxW(NULL, wstr.c_str(), L"服务器收到消息", MB_OK);
	}
}

// 在文件开头添加初始化函数
void InitializeServerName() {
    // 将宽字符串转换为UTF-8
    const wchar_t* defaultName = L"赤炎魔兽";
    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, defaultName, -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length > 0) {
        WideCharToMultiByte(CP_UTF8, 0, defaultName, -1, serverName, sizeof(serverName), nullptr, nullptr);
    }
}

// TcpServer实现
TcpServer::TcpServer(asio::io_context& io_context, short port)
    : acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    , isRunning(false)
{
    LoadNotice();  // 加载通知
    LoadDataFiles();  // 加载数据文件
}

void TcpServer::Start() {
    isRunning = true;
    StartAccept();
}

void TcpServer::Stop() {
    isRunning = false;
    acceptor_.close();

    // 关闭所有客户端连接
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto& socket : clients) {
        if (socket->is_open()) {
            asio::error_code ec;
            socket->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            socket->close(ec);
        }
    }
    clients.clear();
}

void TcpServer::StartAccept() {
    auto socket = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*socket,
        std::bind(&TcpServer::HandleAccept, this, socket,
            std::placeholders::_1));
}

void TcpServer::HandleAccept(std::shared_ptr<asio::ip::tcp::socket> socket,
                           const asio::error_code& error) {
    if (!error && isRunning) {
        try {
            // 获取客户端连接信
            asio::ip::tcp::endpoint remote_ep = socket->remote_endpoint();
            std::string client_ip = remote_ep.address().to_string();
            unsigned short client_port = remote_ep.port();

            // 存储客户端连接
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.push_back(socket);
            }

            //// 显示连接信息
            //std::wstring msg = L"新客户端连接\n"
            //                  L"IP: " + std::wstring(client_ip.begin(), client_ip.end()) + 
            //                  L"\n端口: " + std::to_wstring(client_port) +
            //                  L"\n当前连接数: " + std::to_wstring(clients.size());
            //MessageBoxW(NULL, msg.c_str(), L"连接信息", MB_OK);

            auto buffer = std::make_shared<asio::streambuf>();
            
            // 先读取所有可用数据
            asio::async_read_until(*socket, *buffer, "<END_OF_MESSAGE>",
                [this, socket, buffer](const asio::error_code& error, std::size_t bytes_transferred) 
                {
                    HandleRead(socket, buffer, error, bytes_transferred);
                });
        }
        catch (const std::exception& e) {
            // 如果获取户端信息失败，显示错误
            std::string error_msg = "获取客户端信息失败: " + std::string(e.what());
            int wlen = MultiByteToWideChar(CP_UTF8, 0, error_msg.c_str(), -1, NULL, 0);
            std::wstring wstr(wlen, 0);
            MultiByteToWideChar(CP_UTF8, 0, error_msg.c_str(), -1, &wstr[0], wlen);
            MessageBoxW(NULL, wstr.c_str(), L"错误", MB_OK);
        }

        StartAccept();
    }
}

void TcpServer::HandleRead(std::shared_ptr<asio::ip::tcp::socket> socket,
                         std::shared_ptr<asio::streambuf> buffer,
                         const asio::error_code& error,
                         std::size_t bytes_transferred) {
    if (!error) {
        // 从buffer中提取所有数据
        std::string data{
            asio::buffers_begin(buffer->data()),
            asio::buffers_begin(buffer->data()) + bytes_transferred
        };
        buffer->consume(bytes_transferred);  // 清空缓冲区

        // 查找消息结束标记
        size_t endPos = data.find("<END_OF_MESSAGE>");
        if (endPos != std::string::npos) {
            // 提取有效消息内容
            std::string command = data.substr(0, endPos);
            // 处理命令
            HandleCommand(socket, command);
        }

        // 继续读下一个消息
        asio::async_read_until(*socket, *buffer, "<END_OF_MESSAGE>",
            [this, socket, buffer](const asio::error_code& error, std::size_t bytes_transferred) {
                HandleRead(socket, buffer, error, bytes_transferred);
            });
    }
    else {
        // 处理错误，如客户端断开连接
        try {
            asio::ip::tcp::endpoint remote_ep = socket->remote_endpoint();
            std::string client_ip = remote_ep.address().to_string();
            unsigned short client_port = remote_ep.port();

            // 从容器中移除断开的客户端
            {
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.erase(std::remove_if(clients.begin(), clients.end(),
                    [&socket](const auto& s) { return s == socket; }), clients.end());
            }

            // 显示断开连接信息
            std::wstring msg = L"客户端断开连接\n"
                              L"IP: " + std::wstring(client_ip.begin(), client_ip.end()) + 
                              L"\n端口: " + std::to_wstring(client_port) +
                              L"\n当前连接数: " + std::to_wstring(clients.size());
            MessageBoxW(NULL, msg.c_str(), L"连接信息", MB_OK);
        }
        catch (...) {
            // 如果获取客户端信息失败，直接移除socket
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.erase(std::remove_if(clients.begin(), clients.end(),
                [&socket](const auto& s) { return s == socket; }), clients.end());
        }
    }
}

void TcpServer::HandleCommand(std::shared_ptr<asio::ip::tcp::socket> socket,const std::string& command)
{ 
    // 检查命令是否包含分隔符 "|"
    size_t separatorPos = command.find("|");
    if (separatorPos == std::string::npos) {
        SendResponse(socket, "\xEF\xBB\xBF" "ERROR|Invalid command format<END_OF_MESSAGE>\n");
        return;
    }

    // 提取命令头部
    std::string cmdHeader = command.substr(0, separatorPos + 1);
    std::string cmdContent = command.substr(separatorPos + 1);

    // 根据命令头部处理不同的业务
    if (cmdHeader == Command::INIT_SERVER_INFO) {
        // 构造服务器信息和通知的组合响应
        std::string processedContent = noticeContent;
                
        // 处理通知内容中的换行符，将其替换为特殊标记
        std::string::size_type pos = 0;
        while ((pos = processedContent.find('\n', pos)) != std::string::npos) {
            processedContent.replace(pos, 1, "\\n");  // 使用 "\\n" 替换 "\n"
            pos += 2;  // 的字符
        }

        // 构造组合响应：SERVER_INFO|IP|端口|服务器名称|通知内容
        std::string combinedResponse = 
                                     "SERVER_INFO|" + 
                                     m_serverIP + "|" + 
                                     std::to_string(m_serverPort) + "|" + 
                                     m_serverName + "|" + 
                                     processedContent +
                                     "<END_OF_MESSAGE>";        

        //ConvertAndShowMessage(combinedResponse);

        SendResponse(socket, combinedResponse);
    }
    else if (cmdHeader == Command::CHECK_PATCHES) 
    {
        std::vector<std::string> needUpdateFiles;    // 存储需要更新的文件（CRC不匹配）
        std::vector<std::string> needDeleteFiles;    // 存储需要删除的文件（服务器不存在）
        std::vector<std::string> tokens;
        
        // 分割字符串
        std::string token;
        std::istringstream tokenStream(cmdContent);
        while (std::getline(tokenStream, token, '|')) {
            if (!token.empty()) {
                token.erase(0, token.find_first_not_of(" \t\n\r"));
                token.erase(token.find_last_not_of(" \t\n\r") + 1);
                tokens.push_back(token);
            }
        }

        // 创建一个集合存储客户端的所有文件名
        std::set<std::string> clientFiles;
        for (size_t i = 0; i < tokens.size() - 1; i += 2) {
            clientFiles.insert(tokens[i]);
        }

        // 检查客户端发来的文件
        for (size_t i = 0; i < tokens.size() - 1; i += 2) {
            std::string filename = tokens[i];
            try {
                size_t clientCrc = std::stoull(tokens[i + 1]);
                auto it = fileHashes.find(filename);

                if (it == fileHashes.end()) {
                    needDeleteFiles.push_back(filename);
                }
                else if (it->second != clientCrc) {
                    needUpdateFiles.push_back(filename);
                }
            }
            catch (const std::exception&) {
                continue;
            }
        }

        // 检查服务器独有的文件
        for (const auto& [filename, hash] : fileHashes) {
            if (clientFiles.find(filename) == clientFiles.end()) {
                needUpdateFiles.push_back(filename);
            }
        }

        // 1. 首先发送需要删除的文件列表
        if (!needDeleteFiles.empty()) {
            std::string deleteCommand = "DELETE_FILES|";
            for (const auto& file : needDeleteFiles) {
                deleteCommand += file + "|";
            }
            deleteCommand += "<END_OF_MESSAGE>";
            //ConvertAndShowMessage(deleteCommand);
            SendResponse(socket, deleteCommand);
        }

        // 2. 然后发送需要更新的文件
        if (!needUpdateFiles.empty()) {
            for (const auto& filename : needUpdateFiles) {
                std::string filepath = "Data/" + filename;
                std::ifstream file(filepath, std::ios::binary);
                if (!file.is_open()) continue;

                // 获取文件大小
                file.seekg(0, std::ios::end);
                std::streamsize fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                // 构造包头
                std::string header = "UPDATE_FILES|" + filename + "|" + std::to_string(fileSize) + "|<START_CONTENT>|";

                // 读取文件内容到临时缓冲区
                std::vector<char> fileData(fileSize);
                file.read(fileData.data(), fileSize);
                file.close();

                // 构造完整消息
                std::string fullMessage = header;
                fullMessage.append(fileData.data(), fileSize);
                fullMessage += "|<END_CONTENT>|<END_OF_MESSAGE>";

                // 检查最终消息大小
                std::ostringstream finalCheck;
                finalCheck << "最终消息大小检查:\n"
                          << "文件大小: " << std::to_string(fileSize) << "\n"
                          << "完整消息: " << std::to_string(fullMessage.size());
                ConvertAndShowMessage(finalCheck.str());

                // 发送完整消息
                SendResponse(socket, fullMessage);
            }
        }

    }
    else {
        SendResponse(socket, "ERROR|Unknown command<END_OF_MESSAGE>\n");
    }
}

void TcpServer::SendResponse(std::shared_ptr<asio::ip::tcp::socket> socket,
                           const std::string& response) {
    auto responseBuffer = std::make_shared<std::string>(response);
    asio::async_write(*socket,
        asio::buffer(*responseBuffer),
        [responseBuffer](const asio::error_code& error, std::size_t /*bytes_transferred*/) {
            if (error) {
                // 处理错误
            }
        });
}

void TcpServer::LoadNotice() {
    std::ifstream file("G.txt", std::ios::binary);
    if (file.is_open()) {
        // 检查 BOM
        char bom[3];
        file.read(bom, 3);
        if (!(bom[0] == (char)0xEF && bom[1] == (char)0xBB && bom[2] == (char)0xBF)) {
            // 如果文件不是以 BOM 开头，重置文件指针到开始
            file.seekg(0);
        }

        // 取文件内容
        std::string fileContent((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
        file.close();

        // 存储 UTF-8 编码的内容
        noticeContent = fileContent;

        // 转换为宽字符以供显示
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, noticeContent.c_str(), -1, nullptr, 0);
        if (wideSize > 0) {
            std::vector<wchar_t> wstr(wideSize);
            if (MultiByteToWideChar(CP_UTF8, 0, noticeContent.c_str(), -1, wstr.data(), wideSize) > 0)
            {
                //std::wstring debugMsg = L"成功读取通知文件 G.txt\n内容长度: " + 
                //                      std::to_wstring(noticeContent.length()) + 
                //                      L" 字节\n\n内容:\n" + 
                //                      wstr.data();
                //MessageBoxW(NULL, debugMsg.c_str(), L"通知文件加载", MB_OK);
            }
        }
    }
    else {
        MessageBoxW(NULL, L"无法打开通知文件 G.txt\n请确认文件存在且可访问", L"错误", MB_OK);
    }
}

void TcpServer::LoadDataFiles() {
    namespace fs = std::filesystem;
    std::hash<std::string_view> hasher;

    try {
        // 检查 Data 目录是否存在
        if (!fs::exists("Data")) {
            MessageBoxW(NULL, L"Data 目录不存在", L"错误", MB_OK);
            return;
        }

        // 遍历当前目录下的 Data 文件夹
        for (const auto& entry : fs::directory_iterator("Data")) {

            if (entry.is_regular_file() && 
                (entry.path().extension() == ".mpq" || entry.path().extension() == ".MPQ")) {
                // 读取文件名和完整路径
                std::string filename = entry.path().filename().string();
                std::string fullPath = entry.path().string();

                // 打开文件
                std::ifstream file(entry.path(), std::ios::binary);
                if (!file.is_open()) {
                    std::wstring errorMsg = L"无法打开文件: " + 
                        std::wstring(fullPath.begin(), fullPath.end());
                    MessageBoxW(NULL, errorMsg.c_str(), L"错误", MB_OK);
                    continue;
                }

                // 获取文件大小
                file.seekg(0, std::ios::end);
                size_t filesize = file.tellg();
                file.seekg(0, std::ios::beg);

                // 使用缓冲区读取文件，提高性能
                const size_t buffer_size = 8192;  // 8KB 缓冲区
                std::vector<char> buffer(buffer_size);
                size_t crc = 0;

                while (file) {
                    file.read(buffer.data(), buffer_size);
                    std::streamsize count = file.gcount();
                    if (count > 0) {
                        // 只计算实际读取的数据
                        crc ^= hasher(std::string_view(buffer.data(), count));
                    }
                }
                file.close();
                // 存储到器中
                fileHashes[filename] = crc;
            }
        }

    }
    catch (const std::exception& e) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, NULL, 0);
        std::wstring wstr(wlen, 0);
        MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wstr[0], wlen);
        MessageBoxW(NULL, wstr.c_str(), L"错误", MB_OK);
    }
}

void MainWindow() {
    static bool initialized = false;
    if (!initialized) {
        InitializeServerName();
        initialized = true;
    }

    static bool open = true;

    if (open) {
        // 设置窗口填满整个客户端区域
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        
        // 修改窗口标志
        ImGui::Begin("魔兽世界服务端", &open,
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoNavFocus
        );

        ImVec2 windowSize = ImGui::GetWindowSize();
        
        // 服务器配置区域
        ImGui::BeginGroup();
        {
            // IP地址输入
            ImGui::Text("服务器IP:");
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            ImGui::InputText("##ServerIP", serverIP, sizeof(serverIP));
            ImGui::PopItemWidth();

            // 端口号输入
            ImGui::Text("端口号:");
            ImGui::SameLine();
            ImGui::PushItemWidth(100);
            ImGui::InputInt("##ServerPort", &serverPort, 0, 0);
            ImGui::PopItemWidth();

            // 确保端口号在有效范围内
            if (serverPort < 1) serverPort = 1;
            if (serverPort > 65535) serverPort = 65535;

            // 服务器名称输入
            ImGui::Text("服务器名称:");
            ImGui::SameLine();
            ImGui::PushItemWidth(200);
            ImGui::InputText("##ServerName", serverName, sizeof(serverName));
            ImGui::PopItemWidth();
        }
        ImGui::EndGroup();

        // 启动/停止按钮
        float buttonWidth = 150;
        float buttonHeight = 60;
        float padding = 10;
        ImGui::SetCursorPos(ImVec2(
            windowSize.x - buttonWidth - padding,
            windowSize.y - buttonHeight - padding
        ));

        if (!g_serverRunning) {
            if (ImGui::Button("启动服务", ImVec2(buttonWidth, buttonHeight))) {
                try {
                    g_io_context = std::make_unique<asio::io_context>();
                    g_server = std::make_unique<TcpServer>(*g_io_context, serverPort);

                    // 设置服务器配置
                    g_server->SetServerConfig(serverIP, serverPort, std::string(serverName));
                    
                    g_server->Start();
                    g_serverRunning = true;

                    g_io_thread = std::make_unique<std::thread>([&]() {
                        g_io_context->run();
                    });

                    // 转换服务器名称为宽字符用于显示
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, serverName, -1, nullptr, 0);
                    std::vector<wchar_t> wideName(wideLen);
                    MultiByteToWideChar(CP_UTF8, 0, serverName, -1, wideName.data(), wideLen);

                    // 显示消息
                    //std::wstring msg = L"服务器已启动\nIP: " + std::wstring(serverIP, serverIP + strlen(serverIP)) + 
                    //                 L"\n端口: " + std::to_wstring(serverPort) + 
                    //                 L"\n名称: " + std::wstring(wideName.data());
                    //MessageBoxW(NULL, msg.c_str(), L"服务器状态", MB_OK);
                }
                catch (const std::exception& e) {
                    int wlen = MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, NULL, 0);
                    std::wstring wstr(wlen, 0);
                    MultiByteToWideChar(CP_UTF8, 0, e.what(), -1, &wstr[0], wlen);
                    MessageBoxW(NULL, wstr.c_str(), L"错误", MB_OK);
                }
            }
        }
        else {
            if (ImGui::Button("停止服务", ImVec2(buttonWidth, buttonHeight))) {
                if (g_server) {
                    g_server->Stop();
                    g_io_context->stop();
                    if (g_io_thread && g_io_thread->joinable()) {
                        g_io_thread->join();
                    }
                    g_server.reset();
                    g_io_context.reset();
                    g_io_thread.reset();
                    g_serverRunning = false;
                    MessageBoxW(NULL, L"服务器已停止", L"服务器状态", MB_OK);
                }
            }
        }

        // 显示服务器状态
        ImGui::SetCursorPos(ImVec2(padding, windowSize.y - buttonHeight - padding));
        ImGui::Text("服务器状态: %s", g_serverRunning ? "运行中" : "已停止");

        ImGui::End();
    }
    else {
        if (g_server) {
            g_server->Stop();
            g_io_context->stop();
            if (g_io_thread && g_io_thread->joinable()) {
                g_io_thread->join();
            }
        }
        exit(0);
    }
}
