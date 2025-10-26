#!/usr/bin/env python3

import sqlite3
import argparse
from collections import defaultdict

def get_consecutive_failures(db_path, failure_threshold=3):
    """
    从数据库中读取连续失败指定次数的IP地址详细信息
    """
    # 连接数据库
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()
    
    # 首先获取所有IP地址
    cursor.execute("SELECT ip, hostname FROM hosts")
    hosts = cursor.fetchall()
    
    # 用于存储所有ping结果
    all_results = []
    
    # 为每个IP地址查询其对应的表
    for ip, hostname in hosts:
        # 将IP地址转换为表名
        table_name = "ip_" + ip.replace(".", "_")
        
        # 查询该IP的ping结果
        query = f"""
            SELECT delay, success, timestamp
            FROM {table_name}
            ORDER BY timestamp
        """
        
        try:
            cursor.execute(query)
            results = cursor.fetchall()
            
            # 为每个结果添加IP和主机名信息
            for delay, success, timestamp in results:
                all_results.append((ip, hostname, delay, success, timestamp))
        except sqlite3.OperationalError:
            # 如果表不存在，跳过该IP
            continue
    
    # 按IP和时间戳排序所有结果
    all_results.sort(key=lambda x: (x[0], x[4]))
    
    # 处理结果，找出连续失败指定次数的IP
    consecutive_failures = defaultdict(list)
    current_streak = defaultdict(int)
    last_status = {}
    failure_records = defaultdict(list)  # 保存当前IP的连续失败记录
    
    for ip, hostname, delay, success, timestamp in all_results:
        # 转换success为布尔值
        success_bool = bool(success)
        
        # 更新连续失败计数
        if not success_bool:
            # 如果当前是失败，增加连续失败计数
            if ip in last_status and not last_status[ip]:
                current_streak[ip] += 1
            else:
                current_streak[ip] = 1
                failure_records[ip] = []  # 开始新的连续失败记录
            
            # 保存失败记录
            failure_records[ip].append({
                'hostname': hostname,
                'delay': delay,
                'timestamp': timestamp
            })
        else:
            # 如果当前是成功，重置连续失败计数和记录
            current_streak[ip] = 0
            failure_records[ip] = []
        
        # 保存当前状态
        last_status[ip] = success_bool
        
        # 如果连续失败达到阈值，记录详细信息
        if current_streak[ip] >= failure_threshold:
            # 只保存最近的failure_threshold条记录
            consecutive_failures[ip] = failure_records[ip][-failure_threshold:]
    
    conn.close()
    return consecutive_failures

def print_failure_details(consecutive_failures):
    """
    打印连续失败的IP地址详细信息
    """
    if not consecutive_failures:
        print("没有找到连续失败三次的IP地址。")
        return
    
    print("连续失败三次的IP地址：")
    print("-" * 50)
    
    for ip, failures in consecutive_failures.items():
        # 获取主机名（假设所有记录的主机名相同）
        hostname = failures[0]['hostname'] if failures else ""
        # 格式化输出：IP地址(主机名)
        host_info = f"{ip} ({hostname})" if hostname else ip
        
        print(f"{host_info}:")
        
        # 只打印时间戳
        for failure in failures:
            print(f"  {failure['timestamp']}")
        
        print()

def main():
    parser = argparse.ArgumentParser(description='分析ping监控数据库中的连续失败记录')
    parser.add_argument('database', help='SQLite数据库文件路径')
    parser.add_argument('-t', '--threshold', type=int, default=3, 
                        help='连续失败阈值（默认：3）')
    
    args = parser.parse_args()
    
    try:
        consecutive_failures = get_consecutive_failures(args.database, args.threshold)
        print_failure_details(consecutive_failures)
    except Exception as e:
        print(f"错误：{e}")

if __name__ == "__main__":
    main()