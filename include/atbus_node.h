﻿/**
 * libatbus.h
 *
 *  Created on: 2015年10月29日
 *      Author: owent
 */

#pragma once

#ifndef LIBATBUS_NODE_H_
#define LIBATBUS_NODE_H_

#include <bitset>
#include <ctime>
#include <map>

#include "std/smart_ptr.h"
#include "std/functional.h"
#include "design_pattern/noncopyable.h"

#include "detail/libatbus_error.h"
#include "detail/libatbus_config.h"
#include "detail/libatbus_channel_export.h"

#include "atbus_endpoint.h"

namespace flatbuffers {
    class FlatBufferBuilder;
}

namespace atbus {
    namespace protocol {
        struct msg;
    }

    class node: public util::design_pattern::noncopyable {
    public:
        typedef std::shared_ptr<node> ptr_t;

        typedef ATBUS_MACRO_BUSID_TYPE bus_id_t;
        typedef struct {
            enum type {
                EN_CONF_GLOBAL_ROUTER,                  /** 全局路由表 **/
                RESETTING,                              /** 正在重置 **/
                EN_CONF_MAX
            };
        } flag_t;

        typedef struct {
            enum type {
                CREATED = 0,
                INITED,
                LOST_PARENT,
                CONNECTING_PARENT,
                RUNNING,
                CLOSING,
            };
        } state_t;

        typedef struct {
            adapter::loop_t* ev_loop;
            uint32_t children_mask;                     /** 子节点掩码 **/
            std::bitset<flag_t::EN_CONF_MAX> flags;     /** 开关配置 **/
            std::string father_address;                 /** 父节点地址 **/
            int loop_times;                             /** 消息循环次数限制，防止某些通道繁忙把其他通道堵死 **/
            int ttl;                                    /** 消息转发跳转限制 **/

            // ===== 连接配置 =====
            int backlog;
            time_t first_idle_timeout;                  /** 第一个包允许的空闲时间，秒 **/
            time_t ping_interval;                       /** ping包间隔，秒 **/
            time_t retry_interval;                      /** 重试包间隔，秒 **/
            size_t fault_tolerant;                      /** 容错次数，次 **/

            // ===== 缓冲区配置 =====
            size_t msg_size;                            /** 数据包大小 **/
            size_t recv_buffer_size;                    /** 接收缓冲区，和数据包大小有关 **/
            size_t send_buffer_size;                    /** 发送缓冲区限制 **/
            size_t send_buffer_number;                  /** 发送缓冲区静态Buffer数量限制，0则为动态缓冲区 **/
        } conf_t;

        typedef std::map<bus_id_t, endpoint::ptr_t> endpoint_collection_t;

        // ================== 用这个来取代C++继承，减少层次结构 ==================
        struct no_stream_channel_t {
            void* channel;
            key_t key;
            int(*proc_fn)(node&, no_stream_channel_t*, time_t, time_t);
            int(*free_fn)(node&, no_stream_channel_t*);
        };

    public:
        static void default_conf(conf_t* conf);

    private:
        node();
        static ptr_t create();

    public:

        ~node();

        /**
         * @brief 数据初始化
         * @return 0或错误码
         */
        int init(bus_id_t id, const conf_t* conf);

        /**
         * @brief 启动连接流程
         * @return 0或错误码
         */
        int start();

        /**
         * @brief 数据重置（释放资源）
         * @return 0或错误码
         */
        int reset();

        /**
         * @brief 执行一帧
         * @param sec 当前时间-秒
         * @param sec 当前时间-微秒
         * @return 本帧处理的消息数
         */
        int proc(time_t sec, time_t usec);

        /**
         * @brief 监听数据接收地址
         * @param addr 监听地址
         * @param is_caddr 是否是控制节点
         * @return 0或错误码
         */
        int listen(const char* addr);

        /**
         * @brief 连接到目标地址
         * @param addr 连接目标地址
         * @return 0或错误码
         */
        int connect(const char* addr);

        /**
         * @brief 断开到目标的连接
         * @param id 目标ID
         * @return 0或错误码
         */
        int disconnect(bus_id_t id);


        /**
         * @brief 发送数据
         * @param tid 发送目标ID
         * @param type 自定义类型，将作为msg.head.type字段传递。可用于业务区分服务类型
         * @param buffer 数据块地址
         * @param s 数据块长度
         * @return 0或错误码
         * @note 接收端收到的数据很可能不是地址对齐的，所以这里不建议发送内存数据
         *       如果非要发送内存数据的话，一定要memcpy，不能直接类型转换，除非手动设置了地址对齐规则
         */
        int send_data(bus_id_t tid, int type, const void* buffer, size_t s);

        /**
         * @brief 发送消息
         * @param tid 发送目标ID
         * @param mb 消息构建器
         */
        int send_msg(bus_id_t tid, flatbuffers::FlatBufferBuilder& mb);

    public:
        channel::io_stream_channel* get_iostream_channel();
    private:
        adapter::loop_t* get_evloop(); 
        channel::io_stream_conf* get_iostream_conf();

    public:
        inline bus_id_t get_id() const { return self_->get_id(); }
        inline const conf_t& get_conf() const { return conf_; }
        ptr_t get_watcher();

        bool is_child_node(bus_id_t id) const;
        bool is_brother_node(bus_id_t id) const;
        bool is_parent_node(bus_id_t id) const;

        static int get_pid();
        static const std::string& get_hostname();
        static bool set_hostname(const std::string& hn);

        bool add_proc_connection(connection::ptr_t conn);
        bool remove_proc_connection(const std::string& conn_key);

        bool add_connection_timer(connection::ptr_t conn);

        void on_recv(connection* conn, const protocol::msg* m, int status, int errcode);

        void on_recv_data(connection* conn, int type, const void* buffer, size_t s) const;

        int on_error(const endpoint*, const connection*, int, int);
        int on_disconnect(const connection*);
        int on_new_connection(connection*);

        inline const detail::buffer_block* get_temp_static_buffer() const { return static_buffer_; }
        inline detail::buffer_block* get_temp_static_buffer() { return static_buffer_; }

    private:
        static endpoint* find_child(endpoint_collection_t& coll, bus_id_t id);

        static bool insert_child(endpoint_collection_t& coll, endpoint::ptr_t ep);

        static bool remove_child(endpoint_collection_t& coll, bus_id_t id);
    private:
        // ============ 基础信息 ============
        // ID
        endpoint::ptr_t self_;
        state_t::type state_;
        // 配置
        conf_t conf_;
        std::weak_ptr<node> watcher_; // just like std::shared_from_this<T>

        // ============ IO事件数据 ============
        std::list<std::string> listen_address_;
        // 事件分发器
        adapter::loop_t* ev_loop_;
        std::shared_ptr<channel::io_stream_channel> iostream_channel_;
        std::unique_ptr<channel::io_stream_conf> iostream_conf_;
        typedef struct {
            std::function<int(const node&, const endpoint&, const connection&, int, const void*, size_t)> on_recv_msg;
            std::function<int(const node&, const endpoint*, const connection*, int, int)> on_error;
            std::function<int(const node&, int)> on_reg;
            std::function<int(const node&, const endpoint*, int)> on_node_down;
            std::function<int(const node&, const endpoint*, int)> on_node_up;
            std::function<int(const node&, int)> on_invalid_connection;
        } evt_t;
        evt_t events_;

        // 轮训接收通道集
        detail::buffer_block* static_buffer_;
        detail::auto_select_map<std::string, connection::ptr_t>::type proc_connections_;

        // 基于事件的通道信息
        // 基于事件的通道超时收集

        // ============ 节点逻辑关系数据 ============
        // 父节点
        struct father_info_t {
            endpoint::ptr_t node_;
            time_t last_action_time_;
        };
        father_info_t node_father_;

        // 兄弟节点
        endpoint_collection_t node_brother_;

        // 子节点
        endpoint_collection_t node_children_;

        // 全局路由表

        // 统计信息
    };
}

#endif /* LIBATBUS_NODE_H_ */
