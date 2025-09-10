#include "thrift_connect_pool.h"

// Thrift连接池
static std::mutex m_mtxI2103ThriftClient;	// 针对m_vecI2103ThriftClient访问的互斥锁
static SP_I2103_THRIFT_CLIENT_VEC m_vecI2103ThriftClient;	// Thrift连接池
static string strHostIP;
static int iHostPort;

void set_connect_ipport(const char *ip, int port)
{
    strHostIP = ip;
    iHostPort = port;
}

BOOL OpenThriftSlaveInvoke(string strInstruIP, int iPort, I2103_THRIFT_CLIENT_T *pI2103ThriftClient)
{
    static uint32_t error_cnt = 0;

    // 初始化客户端
	std::shared_ptr<TSocket> pTSocket(new TSocket(strInstruIP, iPort));
    pTSocket->setConnTimeout(3000); /* 设置为 非阻塞，3s超时 */
    pTSocket->setRecvTimeout(3000); /* 设置为 非阻塞，3s超时 */
    pTSocket->setSendTimeout(3000); /* 设置为 非阻塞，3s超时 */
    pTSocket->setMaxRecvRetries(2); /*  */

	pI2103ThriftClient->pTSocket.swap(pTSocket);
	std::shared_ptr<TTransport> pTransport(new TBufferedTransport(pI2103ThriftClient->pTSocket));
	pI2103ThriftClient->pTTransport.swap(pTransport);
	std::shared_ptr<TProtocol> pTProtocol(new TBinaryProtocol(pI2103ThriftClient->pTTransport));
	pI2103ThriftClient->pTProtocol.swap(pTProtocol);
	// 单端口多服务设定
	{
		// 维护类
		std::shared_ptr<TMultiplexedProtocol> pSIMaintenanceMulProtocol(new TMultiplexedProtocol(pI2103ThriftClient->pTProtocol, "SIMaintenance"));
		pI2103ThriftClient->pSIMaintenanceProtocol.swap(pSIMaintenanceMulProtocol);
		std::shared_ptr<H2103_Slave_Invoke::SIMaintenanceClient> pSIMaintenanceClient(new H2103_Slave_Invoke::SIMaintenanceClient(pI2103ThriftClient->pSIMaintenanceProtocol));
		pI2103ThriftClient->pSIMaintenanceClient.swap(pSIMaintenanceClient);
	}

	// IO类
	{
		std::shared_ptr<TMultiplexedProtocol> pSIOtherMulProtocol(new TMultiplexedProtocol(pI2103ThriftClient->pTProtocol, "SIOther"));
		pI2103ThriftClient->pSIOtherProtocol.swap(pSIOtherMulProtocol);
		std::shared_ptr<H2103_Slave_Invoke::SIOtherClient> pSIOtherClient(new H2103_Slave_Invoke::SIOtherClient(pI2103ThriftClient->pSIOtherProtocol));
		pI2103ThriftClient->pSIOtherClient.swap(pSIOtherClient);
	}

	// 实时监控类
	{
		std::shared_ptr<TMultiplexedProtocol> pSIRealMonitorMulProtocol(new TMultiplexedProtocol(pI2103ThriftClient->pTProtocol, "SIRealMonitor"));
		pI2103ThriftClient->pSIRealMonitorProtocol.swap(pSIRealMonitorMulProtocol);
		std::shared_ptr<H2103_Slave_Invoke::SIRealMonitorClient> pSIRealMonitorClient(new H2103_Slave_Invoke::SIRealMonitorClient(pI2103ThriftClient->pSIRealMonitorProtocol));
		pI2103ThriftClient->pSIRealMonitorClient.swap(pSIRealMonitorClient);
	}

	// 样本检测类
	{
		std::shared_ptr<TMultiplexedProtocol> pSISampleDetectMulProtocol(new TMultiplexedProtocol(pI2103ThriftClient->pTProtocol, "SISampleDetect"));
		pI2103ThriftClient->pSISampleDetectProtocol.swap(pSISampleDetectMulProtocol);
		std::shared_ptr<H2103_Slave_Invoke::SISampleDetectClient> pSISampleDetectClient(new H2103_Slave_Invoke::SISampleDetectClient(pI2103ThriftClient->pSISampleDetectProtocol));
		pI2103ThriftClient->pSISampleDetectClient.swap(pSISampleDetectClient);
	}

	try
	{
		pI2103ThriftClient->pTTransport->open();

        if (error_cnt > 0) {
            LOG("open thrift error resume. count:%d\n", error_cnt);
        }
        error_cnt = 0;
	}
	catch (TException &e)
	{
        if (error_cnt == 0) {
            LOG("%s\n", e.what());
        }
        error_cnt++;  
		return FALSE;
	}
	catch (...)
	{
        LOG("other exception\n");
		return FALSE;
	}

	return TRUE;
}

BOOL CloseThriftSlaveInvoke(I2103_THRIFT_CLIENT_T *pI2103ThriftClient)
{
	if (nullptr == pI2103ThriftClient)
	{
		return FALSE;
	}

	if (nullptr == pI2103ThriftClient->pTTransport)
	{
		return TRUE;
	}
	try
	{
		// 断开连接
		pI2103ThriftClient->pTTransport->close();
	}
	catch (TException &e)
	{
        LOG("%s\n", e.what());        
		return FALSE;
	}
	catch (...)
	{	
        LOG("other exception\n");
		return FALSE;
	}

	return TRUE;
}

BOOL GetThriftClient(SP_I2103_THRIFT_CLIENT_T& spThriftClient)
{
	UNIQUE_LOCK locker(m_mtxI2103ThriftClient);
	if (m_vecI2103ThriftClient.empty())
	{
		SP_I2103_THRIFT_CLIENT_T spI2103ThriftClient = std::make_shared<I2103_THRIFT_CLIENT_T>();
		BOOL bRet = OpenThriftSlaveInvoke(strHostIP, iHostPort, spI2103ThriftClient.get());
		if (bRet)
		{
			m_vecI2103ThriftClient.push_back(spI2103ThriftClient);
		}
	}
	if (m_vecI2103ThriftClient.empty())
	{
		return FALSE;
	}
	spThriftClient = *m_vecI2103ThriftClient.begin();
	m_vecI2103ThriftClient.erase(m_vecI2103ThriftClient.begin());
	return TRUE;
}

void ReturnThriftClient(SP_I2103_THRIFT_CLIENT_T& spThriftClient, BOOL bThriftException)
{
	{
		UNIQUE_LOCK locker(m_mtxI2103ThriftClient);
		m_vecI2103ThriftClient.push_back(spThriftClient);
	}
	// 如果出现了Thrift通信异常，则连接可能已经断开，需释放已经建立的连接 modified by wuxiaohu 20201027
	if (bThriftException)
	{
		ClearThriftClient();
	}
}

void ClearThriftClient(void)
{
	UNIQUE_LOCK locker(m_mtxI2103ThriftClient);
	SP_I2103_THRIFT_CLIENT_VEC::iterator vec_iter = m_vecI2103ThriftClient.begin();
	for (; m_vecI2103ThriftClient.end() != vec_iter; ++vec_iter)
	{
		CloseThriftSlaveInvoke(vec_iter->get());
	}
	m_vecI2103ThriftClient.clear();
}

