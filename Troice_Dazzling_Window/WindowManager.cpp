#include "WindowManager.h"
#include "main.h"


// 全局变量
static std::unique_ptr<asio::io_context> g_io_context;
static std::unique_ptr<TcpServer> g_server;
static std::unique_ptr<std::thread> g_io_thread;
static bool g_serverRunning = false;

// 服务器配置
static char serverIP[256] = "127.0.0.1";     // IP地址输入缓冲区
static int serverPort = 12345;               // 端口号
static char serverName[256] = "";            // 服务器名称

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
}

void TcpServer::Start() {
    isRunning = true;
    StartAccept();
}

void TcpServer::Stop() {
    isRunning = false;
    acceptor_.close();
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
        auto buffer = std::make_shared<asio::streambuf>();
        
        // 使用 async_read_until 读取到换行符
        asio::async_read_until(*socket, *buffer, '\n',
            std::bind(&TcpServer::HandleRead, this, socket, buffer,
                std::placeholders::_1,
                std::placeholders::_2));

        StartAccept();
    }
}

void TcpServer::HandleRead(std::shared_ptr<asio::ip::tcp::socket> socket,
                         std::shared_ptr<asio::streambuf> buffer,
                         const asio::error_code& error,
                         std::size_t bytes_transferred) {
    if (!error) {
        // 从buffer中提取命令
        std::string command;
        std::istream is(buffer.get());
        std::getline(is, command);
        
        // 移除可能的回车符
        if (!command.empty() && command.back() == '\r') {
            command.pop_back();
        }

        // 处理命令
        HandleCommand(socket, command);

        // 继续读取下一个命令
        buffer->consume(bytes_transferred);
        asio::async_read_until(*socket, *buffer, '\n',
            std::bind(&TcpServer::HandleRead, this, socket, buffer,
                std::placeholders::_1,
                std::placeholders::_2));
    }
}

void TcpServer::HandleCommand(std::shared_ptr<asio::ip::tcp::socket> socket,
                           const std::string& command) {
    if (command == Command::GET_NOTICE) {
        // 确保 noticeContent 是 UTF-8 编码
        std::string processedContent = noticeContent;
        
        // 处理换行符
        std::string::size_type pos = 0;
        while ((pos = processedContent.find('\n', pos)) != std::string::npos) {
            processedContent.replace(pos, 1, "\\n");
            pos += 2;
        }

        // 添加 BOM 标记以标识 UTF-8 编码
        std::string utf8Response = std::string("\xEF\xBB\xBF") + "NOTICE|" + processedContent + "<END_OF_MESSAGE>";
        
        //// 调试输出
        //std::wstring debugMsg = L"发送消息长度: " + std::to_wstring(utf8Response.length()) + L" 字节";
        //MessageBoxW(NULL, debugMsg.c_str(), L"发送消息", MB_OK);
        
        SendResponse(socket, utf8Response);
    }
    else if (command == Command::GET_SERVER_INFO) {
        // 将服务器名称从UTF-8转换为宽字符用于显示
        int wideLen = MultiByteToWideChar(CP_UTF8, 0, m_serverName.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wideName(wideLen);
        MultiByteToWideChar(CP_UTF8, 0, m_serverName.c_str(), -1, wideName.data(), wideLen);

        // 构造服务器信息
        std::string serverInfo = std::string("\xEF\xBB\xBF") + "SERVER_INFO|" + 
                                m_serverIP + "|" + 
                                std::to_string(m_serverPort) + "|" + 
                                m_serverName + 
                                "<END_OF_MESSAGE>";
        //
        //// 调试输出
        //std::wstring debugMsg = L"发送服务器信息\nIP: " + std::wstring(m_serverIP.begin(), m_serverIP.end()) + 
        //                       L"\n端口: " + std::to_wstring(m_serverPort) + 
        //                       L"\n名称: " + std::wstring(wideName.data());
        //MessageBoxW(NULL, debugMsg.c_str(), L"发送服务器信息", MB_OK);
        
        SendResponse(socket, serverInfo);
    }
    else if (command == Command::GET_FILE) {
        SendResponse(socket, "\xEF\xBB\xBF" "FILE|未实现<END_OF_MESSAGE>");
    }
    else {
        SendResponse(socket, "\xEF\xBB\xBF" "ERROR|Unknown command<END_OF_MESSAGE>");
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

        // 读取文件内容
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
                    std::wstring msg = L"服务器已启动\nIP: " + std::wstring(serverIP, serverIP + strlen(serverIP)) + 
                                     L"\n端口: " + std::to_wstring(serverPort) + 
                                     L"\n名称: " + std::wstring(wideName.data());
                    MessageBoxW(NULL, msg.c_str(), L"服务器状态", MB_OK);
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
