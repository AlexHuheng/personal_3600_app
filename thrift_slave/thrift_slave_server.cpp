#include <iostream>
#include <stdexcept>
#include <sstream>

#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PlatformThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TSimpleServer.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/processor/TMultiplexedProcessor.h>
#include <thrift/TToString.h>
#include <thrift/stdcxx.h>

#include <HIOtherHandler.h>
#include <HIMaintenanceHandler.h>
#include <HIRealMonitorHandler.h>
#include <HISampleDetectHandler.h>
#include <pthread.h>
#include <log.h>

using namespace std;
using namespace apache::thrift;
using namespace apache::thrift::concurrency;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;

#include "thrift_handler.h"

static int thrift_salve_server_connect_flag = 1; /* 控制 上位机是否连接下位机 0：关闭 1：连接(默认值)*/
static TThreadPoolServer *pool_server = NULL;
static thrift_master_t slave_server = {0};

void thrift_slave_server_restart(const char *ip, int port)
{
    LOG("pool_server: %p, new server ip:port %s:%d\n", pool_server, ip, port);

    memset(&slave_server, 0, sizeof(slave_server.ip));
    strncpy(slave_server.ip, ip, strlen(ip));
    slave_server.port = port;

    if(pool_server){
        pool_server->stop();
        sleep(3);
        pool_server = NULL;
    }

    thrift_slave_server_init(&slave_server);
}

void *thrift_slave_server_run(void *arg)
{
    thrift_master_t *server_param = (thrift_master_t *)arg;

    // This server allows "workerCount" connection at a time, and reuses threads
    const int workerCount = 30;
    stdcxx::shared_ptr<ThreadManager> threadManager = ThreadManager::newSimpleThreadManager(workerCount);
    threadManager->threadFactory(stdcxx::make_shared<PlatformThreadFactory>());
    threadManager->start();
    
    stdcxx::shared_ptr<TMultiplexedProcessor> processor(new TMultiplexedProcessor());

    stdcxx::shared_ptr<HIOtherHandler> hiOtherHandler(new HIOtherHandler());
    stdcxx::shared_ptr<TProcessor> hiOtherProcessor(new HIOtherProcessor(hiOtherHandler));
    processor->registerProcessor("HIOther", hiOtherProcessor);

    stdcxx::shared_ptr<HIMaintenanceHandler> hiMaintenanceHandler(new HIMaintenanceHandler());
    stdcxx::shared_ptr<TProcessor> hiMaintenanceProcessor(new HIMaintenanceProcessor(hiMaintenanceHandler));
    processor->registerProcessor("HIMaintenance", hiMaintenanceProcessor);

    stdcxx::shared_ptr<HIRealMonitorHandler> hiRealMonitorHandler(new HIRealMonitorHandler());
    stdcxx::shared_ptr<TProcessor> hiRealMonitorProcessor(new HIRealMonitorProcessor(hiRealMonitorHandler));
    processor->registerProcessor("HIRealMonitor", hiRealMonitorProcessor);

    stdcxx::shared_ptr<HISampleDetectHandler> hiSampleDetectHandler(new HISampleDetectHandler());
    stdcxx::shared_ptr<TProcessor> hiSampleDetectProcessor(new HISampleDetectProcessor(hiSampleDetectHandler));
    processor->registerProcessor("HISampleDetect", hiSampleDetectProcessor);
    
    stdcxx::shared_ptr<TServerSocket> serverTransport(new TServerSocket(server_param->port));
    stdcxx::shared_ptr<TBufferedTransportFactory> transportFactory(new TBufferedTransportFactory());
    stdcxx::shared_ptr<TBinaryProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    TThreadPoolServer server(processor, serverTransport, transportFactory, protocolFactory, threadManager);

    LOG("Starting the thrift slave server...\n");
    pool_server = &server;
    server.serve();
    LOG("Done\n");

    return NULL;
}

/* 控制 上位机是否连接下位机 0：关闭 1：连接(默认值)*/
void thrift_slave_server_connect_ctl(int flag)
{
    thrift_salve_server_connect_flag = flag;
}

int thrift_slave_server_connect_get()
{
    return thrift_salve_server_connect_flag;
}

void thrift_slave_server_init(const thrift_master_t *thrift_master)
{
    pthread_t tid;

    pthread_create(&tid, NULL, thrift_slave_server_run, (void *)thrift_master);
}


