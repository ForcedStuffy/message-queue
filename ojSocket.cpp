#include "ojSocket.h"
using namespace std;
#include <iostream>
#include <string>
#include <string.h>
#include <vector>
#include <ctime>
#include <queue>
#include <unordered_set>
#include <unordered_map>

const unordered_map<string, function<string(JsonObject &obj)>> callFunName =
{
    {},
    {},
    {},
    {},
    {},
};

//class LinkList
LinkList::LinkList()
{
    nex[_head] = _end;
}

int LinkList::getPre(int x)
{
    return pre[x];
}

int LinkList::getNext(int x)
{
    return nex[x];
}

void LinkList::pop(int x)
{
    _size--;
    nex[pre[x]] = nex[x];
    pre[nex[x]] = pre[x];
}

void LinkList::pushBack(int x)
{
    _size++;
    nex[x] = _end;
    pre[x] = _tail;
    nex[_tail] = x;
    _tail = x;
}

void LinkList::moveBack(int x)
{
    pop(x);
    pushBack(x);
}

int LinkList::front()
{
    return nex[_head];
}

int LinkList::size()
{
    return _size;
}

int LinkList::end()
{
    return _end;
}

int LinkList::tail()
{
    return _tail;
}

//class ClientBuf
ClientBuf::ClientBuf()
{
}

ClientBuf::ClientBuf(int clientFd)
{
    socketFd = clientFd;
}

int ClientBuf::recvReadBuf()
{
    int nread = recvMsg(socketFd, buf);
    addReadBuf(buf, nread);
    return nread;
}

void ClientBuf::addReadBuf(char buffer[], int n)
{
    if (n <= 0)
        return;
    readBuf.insert(readBuf.end(), buffer, buffer + n);
}

void ClientBuf::addWriteBuf(std::string msg)
{
    int msgSize = msg.length();
    char header[HEADER_SIZE] = {0, 0, 0, 0};
    for (int i = 0; i < HEADER_SIZE; i++)
        header[HEADER_SIZE - i - 1] = msgSize >> i * 8 & 0xff;
    writeBuf.insert(writeBuf.end(), header, header + HEADER_SIZE);
    writeBuf.insert(writeBuf.end(), msg.begin(), msg.end());
}

int ClientBuf::sendWriteBuf()
{
    int t = min(BUFSIZE, (int)writeBuf.size());
    for (int i = 0; i < t; i++)
        buf[i] = writeBuf[i];
    int nwrite = sendMsg(socketFd, buf, t);
    for (int i = 0; i < nwrite; i++)
        writeBuf.pop_front();
    return nwrite;
}

std::string ClientBuf::getMessage()
{
    if (readBuf.size() <= HEADER_SIZE)
        return "";
    int datasize = 0;
    for (int i = 0; i < HEADER_SIZE; i++)
        datasize = datasize << 8 | readBuf[i];
    if (readBuf.size() < HEADER_SIZE + datasize)
        return "";
    for (int i = 0; i < HEADER_SIZE; i++)
        readBuf.pop_front();
    std::string res(readBuf.begin(), readBuf.begin() + datasize);
    // readBuf.erase(readBuf.begin(), readBuf.begin() + datasize);
    for (int i = 0; i < datasize; i++) //感觉pop_front()快于erase
        readBuf.pop_front();
    return res;
}

int ClientBuf::push()
{
    int n, sum = 0;
    while (true)
    {
        if (writeBuf.size() == 0)
            return sum;
        n = sendWriteBuf();
        if (n == -1)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                printf("缓冲区数据已经写完\n");
                break;
            }
            else
            {
                perror("write error");
                printf("Error: %s (errno: %d)\n", strerror(errno), errno);
                return -1;
            }
        }
        else if (n == 0)
        {
            perror("连接已断开\n");
            return -1;
        }
        sum += n;
    }
    return sum;
}

int ClientBuf::pull()
{
    int n, sum = 0;
    while (true)
    {
        n = recvReadBuf();
        if (n == -1)
        {
            if (errno == EAGAIN || errno == EINTR)
            {
                printf("缓冲区数据已经读完\n");
                break;
            }
            else
            {
                perror("recv error\n");
                printf("Error: %s (errno: %d)\n", strerror(errno), errno);
                return -1;
            }
        }
        else if (n == 0)
        {
            perror("unlink error\n");
            return -1;
        }
        sum += n;
    }
    return sum;
}

void Scheduler::setClient(int clientFd)
{
    nowClient = clientFd;
    if(clients.find(nowClient) == clients.end())
        clients[nowClient] = ClientBuf();
}
void Scheduler::response(string result, int clientFd)
{
    if(clientFd == -1)
        clients[nowClient].addWriteBuf(result);
    else
        clients[clientFd].addWriteBuf(result);
}

string Scheduler::packageMessage(int code, const string &data)
{
    if(code == 0)
    {
        JsonObject obj(
            unordered_map<string,JsonObject*>{
                {"code",new JsonObject(code)},
                {"data",new JsonObject(data)}
            }
        );
        return obj.json();
    }
    else
    {
        JsonObject obj(
            unordered_map<string,JsonObject*>{
                {"code",new JsonObject(code)},
                {"error",new JsonObject(data)}
            }
        );
        return obj.json();
    }
    return "";
}

string Scheduler::packageMessage(int code, const JsonObject& data)
{
    JsonObject obj(
        unordered_map<string,JsonObject*>{
            {"code",new JsonObject(code)},
            {"data",new JsonObject(data)}
        }
    );
    return obj.json();
}

void Scheduler::readMessage(const string &queue,int MessageId, int count, int block)
{
    auto it = messageQueues.find(queue);
    if (it == messageQueues.end())
    {
        response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
        return;
    }
    MessageQueue &q = it->second;
    int messageId = q.messagePool.getUpperBoundMessageId(messageId);
    // 如果没找到
    if (messageId == -1)
    {
        // 加入阻塞消费者队列
        ull nowTime = getTimeStamp();
        blockLink.insert({nowTime + block ? block : MAX_DELAY_TIME, nowClient});
        // 在当前消费者组加入此等待标识符
        q.waitConsumers.insert(nowClient);
    }
    else
    {
        response(packageMessage(0,q.messagePool.get(messageId).data));
    }
}
void Scheduler::readMessageGroup(const string &queue, const string &group, const string &consumer,int MessageId, int count, int block)
{
    auto qit = messageQueues.find(queue);
    if (qit == messageQueues.end())
    {
        response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
        return;
    }
    MessageQueue &q = qit->second;
    auto git = q.groups.find(group);
    if (git == q.groups.end())
    {
        response(packageMessage(NONE_GROUP,"no name Group:" + group));
        return;
    }
    ConsumerGroup &g = git->second;
    if(g.waitConsumers.find(consumer) != g.waitConsumers.end())
    {
        response(packageMessage(CONSUMER_BUSY,"consumer is waiting:" + group));
        return;
    }
    // 如果当前没有空闲消息
    if(q.Messages.getNext(g.lastId) == q.Messages.end())
    {
        // 不等待直接返回
        if(block == 0)
        {
            response(packageMessage(0, JsonObject(vector<JsonObject *>{})));
            return;
        }

        // 设置好consumer参数
        clientConsumers[nowClient] = {queue, group, consumer};
        q.waitGroups.insert(group);
        g.waitConsumers.insert(consumer);
        Consumer c;
        c.clientFd = nowClient;
        c.name = consumer;
        c.exceptMessageSize = count;
        // 如果block小于0则意味着无限等待
        c.block = block < 0 ? MAX_DELAY_TIME : block;
        c.joinTime = getTimeStamp();
        g.consumers[consumer] = c;
        // 放入block阻塞消费者中
        blockLink.insert({c.joinTime + c.block, nowClient});
        return;
    }
    JsonObject vj(
        vector<JsonObject *>{}
    );
    while(count--)
    {
        if(q.Messages.getNext(g.lastId) == q.Messages.end())
            break;
        g.lastId = q.Messages.getNext(g.lastId);
        vj.asArray().push_back(new JsonObject(q.messagePool.get(g.lastId).data));
    }
    response(packageMessage(0, vj));
}
void Scheduler::addMessage(const string &queue, const string& data)
{
    auto qit = messageQueues.find(queue);
    if (qit == messageQueues.end())
    {
        response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
        return;
    }
    MessageQueue &q = qit->second;
    Message msg;
    msg.data = data;
    int messageId = q.messagePool.put(msg);
    q.Messages.pushBack(messageId);
    vector<string> noWaitGroups;
    for(string group : q.waitGroups)
    {
        ConsumerGroup &g = q.groups[group];

        while(q.Messages.getNext(g.lastId) == q.Messages.end())
        {
            // 随机获取集合中的一个元素
            int size = g.waitConsumers.size();
            int t = rand() % size;
            auto it = g.waitConsumers.begin();
            advance(it, t);

            string consumer = *it;
            Consumer &c = g.consumers[consumer];
            JsonObject vj(
                vector<JsonObject *>{}
            );
            int count = c.exceptMessageSize;
            vector<int> messageIds;
            while(count--)
            {
                if(q.Messages.getNext(g.lastId) == q.Messages.end())
                    break;
                g.lastId = q.Messages.getNext(g.lastId);
                messageIds.push_back(g.lastId);
                vj.asArray().push_back(new JsonObject(q.messagePool.get(g.lastId).data));
            }
            for(int i : messageIds)
                c.messages.pushBack(i);
            response(packageMessage(0, vj), c.clientFd);
            // 从等待消费者中移除
            g.waitConsumers.erase(it);
            // 从阻塞消费者队列中移除
            blockLink.erase({c.joinTime + c.block, c.clientFd});
        }
        if(g.waitConsumers.empty())
            noWaitGroups.push_back(group);
    }
    for(string group : noWaitGroups)
        q.waitGroups.erase(group);
}
void Scheduler::addClient(int clientFd)
{
    clients.insert({clientFd, ClientBuf(clientFd)});
}
void Scheduler::delClient(int clientFd)
{
    clients.erase(clientFd);
}
void Scheduler::delConsumer(const string &queue, const string &group, const string &consumer)
{
    auto qit = messageQueues.find(queue);
    if(qit != messageQueues.end())
    {
        MessageQueue &q = qit->second;

    }
}
void Scheduler::createQueue(const string &queue)
{
    auto qit = messageQueues.find(queue);
    if (qit != messageQueues.end())
    {
        response(packageMessage(2,"Queue already exist:" + queue));
        return;
    }
    messageQueues.insert({queue, MessageQueue()});
    response(packageMessage(CMD_OK, "create queue success:" + queue));
}
void Scheduler::delQueue(const string &queue)
{
    auto qit = messageQueues.find(queue);
    if (qit == messageQueues.end())
    {
        response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
        return;
    }
    messageQueues.erase(qit);
    response(packageMessage(CMD_OK, "create queue success:" + queue));
}
void Scheduler::createGroup(const string &queue, const string &group, bool mkQueue)
{
    auto qit = messageQueues.find(queue);
    if (qit == messageQueues.end())
    {
        if(mkQueue)
        {
            messageQueues.insert({queue, MessageQueue()});
        }
        else
        {
            response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
            return;
        }
    }
    MessageQueue &q = qit->second;
    auto git = q.groups.find(group);
    if (git != q.groups.end())
    {
        response(packageMessage(GROUP_BUSY,"Group already exist:" + group));
        return;
    }
    q.groups.insert({group, ConsumerGroup()});
    response(packageMessage(CMD_OK, "create group success"));
}
void Scheduler::delGroup(const string &queue, const string &group)
{
    auto qit = messageQueues.find(queue);
    if (qit == messageQueues.end())
    {
        response(packageMessage(NONE_QUEUE,"no name Queue:" + queue));
        return;
    }
    MessageQueue &q = qit->second;
    auto git = q.groups.find(group);
    if (git == q.groups.end())
    {
        response(packageMessage(NONE_GROUP,"no name Group:" + group));
        return;
    }
    q.groups.erase(git);
    response(packageMessage(CMD_OK, "remove group success:" + group));
}
void Scheduler::ackMessage(const string &queue, const string &group, int MessageId){}

//linux下
#ifdef __linux__
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <ctype.h>
#include <sys/epoll.h> //epoll头文件

ull getTimeStamp()
{
    struct timeval time;
 
    /* 获取时间，理论到us */
    gettimeofday(&time, NULL);
    return (ull)(tv.tv_usec / 1000) + (ull)tv.tv_sec * 1000;
}

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

static void addEvent(int epollfd, int fd, int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        printf("epoll_ctl Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }
}

static void delEvent(int epollfd, int fd)
{
    if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0)
    {
        printf("epoll_ctl Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }
}

static void modEvent(int epollfd, int fd, int state)
{
    struct epoll_event ev;
    ev.events = state;
    ev.data.fd = fd;
    if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev) < 0)
    {
        printf("epoll_ctl Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }
}

int createServer()
{
    int serverFd;
    struct sockaddr_in st_sersock;
    char msg[MAXSIZE];
    int nrecvSize = 0;

    serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0)
    {
        cerr << "create server failed" << endl;
        exit(-1);
    }
    memset(&st_sersock, 0, sizeof(st_sersock));
    st_sersock.sin_family = AF_INET; //IPv4协议
    st_sersock.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    st_sersock.sin_port = htons(SERVER_PORT);
    if (bind(serverFd, (struct sockaddr *)&st_sersock, sizeof(st_sersock)) < 0) //将套接字绑定IP和端口用于监听
    {
        printf("bind Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    if (listen(serverFd, MAX_LISTEN) < 0) //设定可同时排队的客户端最大连接个数
    {
        printf("listen Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }

    return serverFd;
}

int createClient()
{
    int clientFd;
    struct sockaddr_in st_sersock;
    char msg[MAXSIZE];

    clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0)
    {
        cerr << "create client failed" << endl;
        exit(-1);
    }
    printf("client %d:create success\n", clientFd);
    memset(&st_sersock, 0, sizeof(st_sersock));
    st_sersock.sin_family = AF_INET; //IPv4协议
    st_sersock.sin_addr.s_addr = inet_addr(SERVER_ADDR);
    st_sersock.sin_port = htons(SERVER_PORT);
    if (connect(clientFd, (struct sockaddr *)&st_sersock, sizeof(st_sersock)) < 0)
    {
        perror("connect falied\n");
        exit(1);
    }
    printf("connect success\n");
    setnonblocking(clientFd);
    return clientFd;
}

int recvMsg(int targetFd, char buf[])
{
    int n = recv(targetFd, buf, BUFSIZE, MSG_DONTWAIT);
    return n;
}

int sendMsg(int targetFd, char buf[], int size)
{
    int n = send(targetFd, buf, size, MSG_DONTWAIT);
    return n;
}


void polling(int serverFd, ClientManager cm)
{
    setnonblocking(serverFd);
    struct epoll_event ev, events[MAXSIZE];
    int epfd, nCounts; //epfd:epoll实例句柄, nCounts:epoll_wait返回值
    epfd = epoll_create(MAXSIZE);
    char buf[MAXSIZE];
    if (epfd < 0)
    {
        printf("epoll_create Error: %s (errno: %d)\n", strerror(errno), errno);
        exit(-1);
    }
    addEvent(epfd, serverFd, EPOLLIN | EPOLLERR | EPOLLHUP);
    printf("======waiting for client's request======\n");
    MessageManager pre = cm.mm;
    cout << cm.mm << endl;
    int epollDelayTime = 0;
    while (true)
    {
        // 即使没有标识符事件，仍然会唤醒并且将超时consumer返回
        // epollDelayTime = 当前block队列中的最早时间
        int eventCount = epoll_wait(epfd, events, MAXSIZE, epollDelayTime);
        // printf("events num:%d\n", eventCount);
        if (eventCount < 0)
        {
            printf("epoll_ctl Error: %s (errno: %d)\n", strerror(errno), errno);
            exit(-1);
        }
        else if (eventCount == 0)
        {
            // printf("no data!\n");
        }
        else
        {
            for (int i = 0; i < eventCount; i++)
            {
                int clientFd = events[i].data.fd;
                // cout << "eventFd:" << clientFd << endl;
                //有请求链接
                if (clientFd == serverFd)
                {
                    cout << "建立连接" << endl;
                    int clientFd;
                    while ((clientFd = accept(serverFd, (struct sockaddr *)NULL, NULL)) > 0)
                    {
                        printf("accept Client[%d]\n", clientFd);
                        setnonblocking(clientFd);
                        addEvent(epfd, clientFd, EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLOUT);
                        cm.addUser(clientFd);
                    }
                    continue;
                }
                if ((events[i].events & EPOLLHUP) || (events[i].events & EPOLLERR))
                {
                    printf("%d client err\n", clientFd);
                    delClient(epfd, clientFd, cm);
                    continue;
                }
                if (events[i].events & EPOLLIN)
                {
                    cout << "正在拉取缓冲区" << endl;
                    int n = cm.users[clientFd].pull();
                    printf("recv %d byte\n", n);
                    if (n < 0)
                    {
                        printf("%d close link\n", clientFd);
                        delClient(epfd, clientFd, cm);
                        continue;
                    }
                    string s;
                    while (true)
                    {
                        s = cm.users[clientFd].getMessage();
                        cout << "parse message:" << s << endl;
                        cout << "readbuf size:" << cm.users[clientFd].readBuf.size() << endl;
                        cout << "readbuf:";
                        for (int i = 0; i < cm.users[clientFd].readBuf.size(); i++)
                            cout << cm.users[clientFd].readBuf[i];
                        cout << endl;
                        if (s == "")
                            break;
                        cm.parseMessage(clientFd, s);
                    }
                }
                if (events[i].events & EPOLLOUT)
                {
                    // cout << "正在写入缓冲区" << endl;
                    cm.users[clientFd].push();
                }
            }
        }
    }
}

void closeSocket(int socketFd)
{
    close(socketFd);
}

#endif

//windows
#if defined(WIN32) || defined(WIN64)

int createClient()
{
    return 1;
}

int recvMsg(int targetFd, char buf[])
{
}

int setnonblocking(int socketFd)
{
}

int sendMsg(int targetFd, char buf[], int n)
{
}

void closeSocket(int socketFd)
{
}

#endif
