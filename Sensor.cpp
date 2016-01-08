#include <wiringPi.h>

#include <qcc/platform.h>

#include <signal.h>
#include <stdio.h>
#include <vector>

#include <qcc/String.h>

#include <alljoyn/AllJoynStd.h>
#include <alljoyn/BusAttachment.h>
#include <alljoyn/Init.h>
#include <alljoyn/Status.h>
#include <alljoyn/version.h>

using namespace std;
using namespace qcc;
using namespace ajn;

// ループ用.
static volatile sig_atomic_t sigFlag = false;

static void CDECL_CALL SigIntHandler(int sig)
{
	QCC_UNUSED(sig);
	sigFlag = true;
}

static const char* INTERFACE_NAME = "org.alljoyn.SensorLightCamera.Sensor";
static const char* SERVICE_NAME = "org.alljoyn.SensorLightCamera";
static const char* SERVICE_PATH = "/";
static const SessionPort SERVICE_PORT = 25;

class MyBusObject : public BusObject {
  private:
	bool senseProp;
  public:
	MyBusObject(const char* path, InterfaceDescription* _intf) :
		BusObject(path),
		senseProp(false)
	{
		AddInterface(*_intf);
	}

	void SetSenseProp(bool _senseProp)
	{
		senseProp = _senseProp;
	}

	QStatus Get(const char* ifcName, const char* propName, MsgArg& val)
	{
		QCC_UNUSED(ifcName);

		QStatus status = ER_OK;

		if (0 == strcmp("sense", propName)) {

			val.typeId = ALLJOYN_BOOLEAN;
			val.v_bool = senseProp;

		} else {

			status = ER_BUS_NO_SUCH_PROPERTY;
		}

		return status;
	}
};

class MyBusListener : public BusListener, public SessionPortListener {
private:
	BusAttachment* busAtt;
	String sessionHost;
public:
	MyBusListener(BusAttachment* _busAtt)
	{
		busAtt = _busAtt;
	}

	void NameOwnerChanged(const char* busName, const char* previousOwner, const char* newOwner)
	{
		if (newOwner && (0 == strcmp(busName, SERVICE_NAME))) {
			printf("NameOwnerChanged: name=%s, oldOwner=%s, newOwner=%s.\n",
				   busName,
				   previousOwner ? previousOwner : "<none>",
				   newOwner ? newOwner : "<none>");
		}
	}
	bool AcceptSessionJoiner(SessionPort sessionPort, const char* joiner, const SessionOpts& opts)
	{
		if (sessionPort != SERVICE_PORT) {
			printf("Rejecting join attempt on unexpected session port %d.\n", sessionPort);
			return false;
		}
		printf("Accepting join session request from %s (opts.proximity=%x, opts.traffic=%x, opts.transports=%x).\n",
			   joiner, opts.proximity, opts.traffic, opts.transports);
		return true;
	}
	void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
	{
		BusListener::FoundAdvertisedName(name, transport, namePrefix);
	}
	void LostAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
	{
		BusListener::LostAdvertisedName(name, transport, namePrefix);
	}
	void ListenerRegistered(BusAttachment* bus)
	{
		BusListener::ListenerRegistered(bus);
	}
	void ListenerUnregistered()
	{
		BusListener::ListenerUnregistered();
	}
	void BusStopping()
	{
		BusListener::BusStopping();
	}
	void BusDisconnected()
	{
		BusListener::BusDisconnected();
	}
	void SessionJoined(SessionPort sessionPort, SessionId id, const char* joiner)
	{
		SessionPortListener::SessionJoined(sessionPort, id, joiner);
	}
};

class SensorBus
{
private:
	BusAttachment* busAtt = NULL;
	MyBusObject* busObj = NULL;
	MyBusListener* busListener = NULL;
	const InterfaceDescription::Member* sensed = NULL;

	InterfaceDescription* createInterfaceDescription(BusAttachment* _busAtt)
	{
		InterfaceDescription* intf = NULL;
		_busAtt->CreateInterface(INTERFACE_NAME, intf);

		intf->AddSignal("sensed", "b", "sense", 0);
		intf->AddProperty("sense", "b", PROP_ACCESS_READ);

		intf->Activate();

		sensed = intf->GetSignal("sensed");

		return intf;
	}
public:
	SensorBus()
	{
		AllJoynInit();
		//AllJoynRouterInit();

		printf("AllJoyn Library version: %s.\n", ajn::GetVersion());

		busAtt = new BusAttachment("Sensor", true);

		busListener = new MyBusListener(busAtt);
		busAtt->RegisterBusListener(*busListener);

		busAtt->Start();

		InterfaceDescription* intf = createInterfaceDescription(busAtt);

		busObj = new MyBusObject(SERVICE_PATH, intf);
		busAtt->RegisterBusObject(*busObj);

		busAtt->Connect();

		busAtt->RequestName(SERVICE_NAME, (DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE));

		SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);
		SessionPort sp = SERVICE_PORT;
		busAtt->BindSessionPort(sp, opts, *busListener);

		busAtt->AdvertiseName(SERVICE_NAME, TRANSPORT_ANY);
	}

	~SensorBus()
	{
		delete busAtt;
		busAtt = NULL;
		delete busObj;
		busObj = NULL;
		delete busListener;
		busListener = NULL;

		//AllJoynRouterShutdown();
		AllJoynShutdown();
	}

	void SendSignal(bool ret)
	{
		busObj->SetSenseProp(ret);

		MsgArg* arg = new MsgArg(ALLJOYN_BOOLEAN);
		arg->v_bool = ret;

		printf("Send Signal(%d)\n", ret);

		busObj->Signal(NULL,							// NULL for broadcast signals.
						0,								// For broadcast or sessionless signals, the sessionId must be 0.
						*sensed,						// Interface member of signal being emitted.
						arg, 							// The arguments for the signal (can be NULL).
						1, 								// The number of arguments.
						0, 								// timeToLive.
						ALLJOYN_FLAG_GLOBAL_BROADCAST	// broadcast signal (null destination) will be forwarded to all Routing Nodes in the system.
		);
	}
};

// SendSignal向け関数ポインタ.
typedef void (SensorBus::*SendSignalPtr)(bool);

class HumanSensor
{
private:
	const int pin_22 = 6;	   // rsp board pin:22

	// SensorBusに処理委譲.
	SensorBus* bus;
	SendSignalPtr fp;

	// 静的からローカルに変更.
	static HumanSensor* thisPtr;
	static void staticSig()
	{
		thisPtr->sig();
	}
	void sig()
	{
		printf("sig: %d \n", digitalRead(pin_22));
		(bus->*fp)(digitalRead(pin_22));
	}
public:
	HumanSensor(SendSignalPtr _fp, SensorBus* _bus) :
	bus(_bus), fp(_fp)
	{
		thisPtr = this;

		wiringPiSetup();

		pinMode(pin_22, INPUT);
		pullUpDnControl(pin_22, PUD_UP);
		wiringPiISR(pin_22, INT_EDGE_BOTH, &HumanSensor::staticSig);
	}
	~HumanSensor()
	{
	}
};
HumanSensor* HumanSensor::thisPtr;

// メイン処理.
int CDECL_CALL main(int argc, char** argv, char** envArg)
{
	QCC_UNUSED(argc);
	QCC_UNUSED(argv);
	QCC_UNUSED(envArg);

	SensorBus* bus = new SensorBus();
	HumanSensor* sensor = new HumanSensor(&SensorBus::SendSignal, bus);

	// ctrl-c受付.
	signal(SIGINT, SigIntHandler);

	while (sigFlag == false) {
		usleep(100 * 1000);
	}

	delete bus;
	delete sensor;

	return 0;
}
