#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TTransportUtils.h>
#include <thrift/protocol/TMultiplexedProtocol.h>
#include <mutex>

#include <Defs_types.h>
#include <SIOther.h>
#include <HIRealMonitor.h>
#include <SIMaintenance.h>
#include <SIRealMonitor.h>
#include <SISampleDetect.h>

#include <log.h>
#include <errno.h>
#include <h3600_maintain_utils.h>
#include <h3600_common.h>

using namespace std;
using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;

using std::shared_ptr;
using namespace H2103_Host_Invoke;

#define SHARED_PTR std::shared_ptr
#define MAKE_SHARED std::make_shared
#define UNIQUE_LOCK std::unique_lock<std::mutex>
#define BOOL bool

// Thrift客户端信息
typedef struct tagI2103_THRIFT_CLIENT_T
{
	// 2103下位机作为客户端调用接口类
	SHARED_PTR<TSocket>                pTSocket;
	SHARED_PTR<TTransport>             pTTransport;
	SHARED_PTR<TProtocol>              pTProtocol;
	SHARED_PTR<TMultiplexedProtocol>   pSIMaintenanceProtocol;
	SHARED_PTR<TMultiplexedProtocol>   pSIOtherProtocol;
	SHARED_PTR<TMultiplexedProtocol>   pSIRealMonitorProtocol;
	SHARED_PTR<TMultiplexedProtocol>   pSISampleDetectProtocol;

	SHARED_PTR<H2103_Slave_Invoke::SIMaintenanceClient>   pSIMaintenanceClient;	// 维护类接口
	SHARED_PTR<H2103_Slave_Invoke::SIRealMonitorClient>   pSIRealMonitorClient;	// 实时监控类接口
	SHARED_PTR<H2103_Slave_Invoke::SISampleDetectClient>  pSISampleDetectClient;	// 样本检测接口
	SHARED_PTR<H2103_Slave_Invoke::SIOtherClient>         pSIOtherClient;			// 其它接口
	tagI2103_THRIFT_CLIENT_T()
	{
		pTSocket = NULL;
		pTTransport = NULL;
		pTProtocol = NULL;
		pSIMaintenanceProtocol = NULL;
		pSIOtherProtocol = NULL;
		pSIRealMonitorProtocol = NULL;
		pSISampleDetectProtocol = NULL;
		pSIMaintenanceClient = NULL;
		pSIRealMonitorClient = NULL;
		pSISampleDetectClient = NULL;
		pSIOtherClient = NULL;
	}
} I2103_THRIFT_CLIENT_T;

typedef std::shared_ptr<I2103_THRIFT_CLIENT_T>	SP_I2103_THRIFT_CLIENT_T;
typedef vector<SP_I2103_THRIFT_CLIENT_T>			SP_I2103_THRIFT_CLIENT_VEC;

// 启动Thrift通信（作为客户端）
BOOL OpenThriftSlaveInvoke(string strInstruIP, int iPort, I2103_THRIFT_CLIENT_T *pI2103ThriftClient);

// 关闭Thrift通信（作为客户端）
BOOL CloseThriftSlaveInvoke(I2103_THRIFT_CLIENT_T *pI2103ThriftClient);

// 获取Thrift客户端指针
BOOL GetThriftClient(SP_I2103_THRIFT_CLIENT_T& spThriftClient);

// 归还Thrift客户端指针
void ReturnThriftClient(SP_I2103_THRIFT_CLIENT_T& spThriftClient, BOOL bThriftException);

// 清空Thrift客户端指针
void ClearThriftClient(void);

// 设置连接的IP和端口
void set_connect_ipport(const char *ip, int port);

