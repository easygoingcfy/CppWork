/**
 * @file tcp.cpp
 * @author caofangyu (caofy@antwork.link)
 * @brief  tcp client for moutable device
 * @version 0.1
 * @date 2023-09-25
 *
 * Copyright (c) 2015-2022 Xunyi Ltd. All rights reserved.
 *
 */

#include "tcp.h"

#include <boost/bind.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace flight_brain {
namespace conn {

using boost::asio::io_service;
using boost::asio::ip::tcp;
using boost::system::error_code;

static bool resolve_address_tcp(io_service &io, std::string host, unsigned short port, tcp::endpoint &ep) {
    bool result = false;
    tcp::resolver resolver(io);
    error_code ec;

    tcp::resolver::query query(host, "");

    auto fn = [&](const tcp::endpoint &q_ep) {
        ep = q_ep;
        ep.port(port);
        result = true;
    };

#if BOOST_ASIO_VERSION >= 101200
    for (auto q_ep : resolver.resolve(query, ec)) fn(q_ep);
#else
    std::for_each(resolver.resolve(query, ec), tcp::resolver::iterator(), fn);
#endif

    if (ec) {
        result = false;
    }

    return result;
}

/**
 * @brief Construct a new Conn Tcp Client:: Conn Tcp Client object
 *
 * @param server_host
 * @param server_port
 */
ConnTcpClient::ConnTcpClient(std::string server_host, int server_port) : ConnInterface(), socket_(io_service_) {
    protocol_type_ = "udp";
    if (!resolve_address_tcp(io_service_, server_host, server_port, endpoint_))
        throw DeviceError("tcp: resolve", "Bind address resolve failed");
}

ConnTcpClient::~ConnTcpClient() {
    ROS_INFO("%s desctructed", protocol_type_.c_str());
    is_destroying_ = true;
    lock_guard lock(mutex_);
    for (auto &timer : timer_array_) {
        timer->stop();
    }
    timer_array_.clear();
    close();
}

void ConnTcpClient::connect() {
    boost::asio::socket_base::keep_alive option(true);
    socket_.open(tcp::v4());
    socket_.set_option(option);
    // boost::system::error_code ec;
    // socket_.connect(server_ep_, ec);
    // if (ec) return;
    // conn_handler();
    socket_.async_connect(endpoint_,
                          boost::bind(&ConnTcpClient::conn_handler, shared_from_this(), boost::placeholders::_1));
}

void ConnTcpClient::close() {
    lock_guard lock(mutex_);

    printf("call func close\n");
    if (!is_open()) return;
    printf("call cancel\n");
    socket_.cancel();
    printf("call socket close\n");
    socket_.close();

    io_work_.reset();
    io_service_.stop();
    if (io_thread_.joinable()) io_thread_.join();
    io_service_.reset();

    if (closed_cb_) closed_cb_();
}

void ConnTcpClient::send_message(const std::string &message) {
    {
        lock_guard lock(mutex_);
        tx_queue_.emplace_back(message);
    }
    io_service_.post(boost::bind(&ConnTcpClient::do_send, shared_from_this(), true));
}

void ConnTcpClient::send_bytes(const void *data, size_t len) {
    {
        lock_guard lock(mutex_);
        tx_queue_.emplace_back((const char *)data, len);
    }
    io_service_.post(std::bind(&ConnTcpClient::do_send, shared_from_this(), true));
}

void ConnTcpClient::do_receive() {
    if (is_destroying_) {
        return;
    }
    auto sthis = shared_from_this();
    socket_.async_receive(boost::asio::buffer(read_buffer_), [&sthis](error_code error, size_t bytes_transferred) {
        if (error) {
            ROS_ERROR("receive error: %s", error.message().c_str());
            sthis->reconnect();
            return;
        }
        if (sthis->receive_cb_) sthis->receive_cb_(sthis->read_buffer_, bytes_transferred);
        sthis->do_receive();
    });
}

void ConnTcpClient::do_send(bool check_tx_state) {
    if (check_tx_state && tx_in_progress_) return;

    lock_guard lock(mutex_);
    if (tx_queue_.empty()) return;

    tx_in_progress_ = true;
    auto sthis = shared_from_this();
    // TODO(caofy): 自己编写缓冲区类
    auto &buf_ref = tx_queue_.front();
    printf("send bytes \n");
    socket_.async_send(boost::asio::buffer(buf_ref.data(), buf_ref.length()),
                       [&sthis, &buf_ref](error_code error, size_t bytes_transferred) {
                           assert(bytes_transferred <= buf_ref.length());
                           if (error) {
                               sthis->tx_in_progress_ = false;
                               sthis->reconnect();
                               return;
                           }
                           lock_guard lock(sthis->mutex_);
                           if (sthis->tx_queue_.empty()) {
                               sthis->tx_in_progress_ = false;
                               return;
                           }
                           sthis->tx_queue_.pop_front();

                           if (!sthis->tx_queue_.empty()) {
                               sthis->do_send(false);
                           } else {
                               sthis->tx_in_progress_ = false;
                           }
                       });
}

void ConnTcpClient::reconnect() {
    socket_.close();
    connect();
}

void ConnTcpClient::conn_handler(const boost::system::error_code &ec) {
    if (ec) {
        std::cout << "connect fail: " << ec.message() << std::endl;
        return;
    }
    if (conn_cb_) conn_cb_();
    // give some work to io_service before start
    io_service_.post(std::bind(&ConnTcpClient::do_receive, this));
}
void ConnTcpClient::read_handler(const boost::system::error_code &ec) {
    if (ec) return;
}
void ConnTcpClient::write_handler(const boost::system::error_code &ec) {
    if (ec) return;
}

}  // namespace conn
}  // namespace flight_brain