#pragma once

// ASIO 相关定义
#define ASIO_STANDALONE
#define ASIO_NO_WIN32_LEAN_AND_MEAN
#include <asio.hpp>
#include "Protocol.h"

// 标准库
#include <string>
#include <memory>
#include <thread>
#include <functional>
#include <fstream>
#include <vector>

class TcpServer {
public:
    TcpServer(asio::io_context& io_context, short port);
    void Start();
    void Stop();
    bool IsRunning() const { return isRunning; }
    void LoadNotice();
    
    // 添加配置设置函数
    void SetServerConfig(const std::string& ip, int port, const std::string& name) {
        m_serverIP = ip;
        m_serverPort = port;
        m_serverName = name;
    }

private:
    void StartAccept();
    void HandleAccept(std::shared_ptr<asio::ip::tcp::socket> socket,
                     const asio::error_code& error);
    void HandleRead(std::shared_ptr<asio::ip::tcp::socket> socket,
                   std::shared_ptr<asio::streambuf> buffer,
                   const asio::error_code& error,
                   std::size_t bytes_transferred);
    
    void HandleCommand(std::shared_ptr<asio::ip::tcp::socket> socket,
                      const std::string& command);
    
    void SendResponse(std::shared_ptr<asio::ip::tcp::socket> socket,
                     const std::string& response);

    asio::ip::tcp::acceptor acceptor_;
    bool isRunning;
    std::string noticeContent;

    // 添加服务器配置成员变量
    std::string m_serverIP;
    int m_serverPort;
    std::string m_serverName;
};

void MainWindow();
