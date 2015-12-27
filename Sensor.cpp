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

/** Static top level message bus object */
static BusAttachment* s_msgBus = NULL;

static SessionId s_sessionId = 0;

/*constants*/
static const char* INTERFACE_NAME = "org.alljoyn.Bus.sample";
static const char* SERVICE_NAME = "org.alljoyn.Bus.sample";
static const char* SERVICE_PATH = "/";
static const SessionPort SERVICE_PORT = 25;

static volatile sig_atomic_t s_interrupt = false;

static void CDECL_CALL SigIntHandler(int sig)
{
    QCC_UNUSED(sig);
    s_interrupt = true;
}

static const char* tags[] = { "en", "de" };
static const char* objId = "obj";
static const char* objDescription[] =  { "This is the object", "DE: This is the object" };
static const char* ifcId = "ifc";
static const char* ifcDescription[] =  { "This is the interface", "DE: This is the interface" };
static const char* nameChangedId = "nameChanged";
static const char* nameChangedDescription[] =  { "Emitted when the name changes", "DE: Emitted whent he name changes" };
static const char* argId = "arg";
static const char* nameChangedArgDescription[] =  { "This is the new name", "DE: This is the new name" };
static const char* propId = "prop";
static const char* namePropDescription[] =  { "This is the actual name", "DE: This is the actual name" };

class MyTranslator : public Translator {
  public:

    virtual ~MyTranslator() { }

    virtual size_t NumTargetLanguages() {
        return 2;
    }

    virtual void GetTargetLanguage(size_t index, qcc::String& ret) {
        ret.assign(tags[index]);
    }

    virtual const char* Translate(const char* sourceLanguage, const char* targetLanguage, const char* source) {
        QCC_UNUSED(sourceLanguage);

        size_t i = 0;
        if (targetLanguage && (0 == strcasecmp(targetLanguage, "de"))) {
            i = 1;
        }

        if (0 == strcmp(source, objId)) {
            return objDescription[i];
        }
        if (0 == strcmp(source, ifcId)) {
            return ifcDescription[i];
        }
        if (0 == strcmp(source, nameChangedId)) {
            return nameChangedDescription[i];
        }
        if (0 == strcmp(source, argId)) {
            return nameChangedArgDescription[i];
        }
        if (0 == strcmp(source, propId)) {
            return namePropDescription[i];
        }
        return NULL;
    }

};

class BasicSampleObject : public BusObject {
  public:
    BasicSampleObject(BusAttachment& bus, const char* path) :
        BusObject(path),
        nameChangedMember(NULL),
        prop_name("Default name")
    {
        /* Add org.alljoyn.Bus.signal_sample interface */
        InterfaceDescription* intf = NULL;
        QStatus status = bus.CreateInterface(INTERFACE_NAME, intf);
        if (status == ER_OK) {
            intf->AddSignal("nameChanged", "s", "newName", 0);
            intf->AddProperty("name", "s", PROP_ACCESS_RW);

            intf->SetDescriptionLanguage("");
            intf->SetDescription(ifcId);
            intf->SetMemberDescription("nameChanged", nameChangedId);
            intf->SetArgDescription("nameChanged", "newName", argId);
            intf->SetPropertyDescription("name", propId);

            intf->SetDescriptionTranslator(&translator);

            intf->Activate();
        } else {
            printf("Failed to create interface %s\n", INTERFACE_NAME);
        }

        status = AddInterface(*intf);

        if (status == ER_OK) {
            /* Register the signal handler 'nameChanged' with the bus */
            nameChangedMember = intf->GetMember("nameChanged");
            assert(nameChangedMember);
        } else {
            printf("Failed to Add interface: %s", INTERFACE_NAME);
        }

        SetDescription("", objId);
        SetDescriptionTranslator(&translator);
    }

    QStatus EmitNameChangedSignal(qcc::String newName)
    {
        printf("Emiting Name Changed Signal.\n");
        assert(nameChangedMember);
        if (0 == s_sessionId) {
            printf("Sending NameChanged signal without a session id\n");
        }
        MsgArg arg("s", newName.c_str());
        uint8_t flags = ALLJOYN_FLAG_GLOBAL_BROADCAST;
        QStatus status = Signal(NULL, 0, *nameChangedMember, &arg, 1, 0, flags);

        return status;
    }

    QStatus Get(const char* ifcName, const char* propName, MsgArg& val)
    {
        QCC_UNUSED(ifcName);

        printf("Get 'name' property was called returning: %s\n", prop_name.c_str());
        QStatus status = ER_OK;
        if (0 == strcmp("name", propName)) {
            val.typeId = ALLJOYN_STRING;
            val.v_string.str = prop_name.c_str();
            val.v_string.len = prop_name.length();
        } else {
            status = ER_BUS_NO_SUCH_PROPERTY;
        }
        return status;
    }

    QStatus Set(const char* ifcName, const char* propName, MsgArg& val)
    {
        QCC_UNUSED(ifcName);

        QStatus status = ER_OK;
        if ((0 == strcmp("name", propName)) && (val.typeId == ALLJOYN_STRING)) {
            printf("Set 'name' property was called changing name to %s\n", val.v_string.str);
            prop_name = val.v_string.str;
            EmitNameChangedSignal(prop_name);
        } else {
            status = ER_BUS_NO_SUCH_PROPERTY;
        }
        return status;
    }
  private:
    const InterfaceDescription::Member* nameChangedMember;
    qcc::String prop_name;
    MyTranslator translator;
};

class MyBusListener : public BusListener, public SessionPortListener {
    void NameOwnerChanged(const char* busName, const char* previousOwner, const char* newOwner)
    {
        if (newOwner && (0 == strcmp(busName, SERVICE_NAME))) {
            printf("NameOwnerChanged: name=%s, oldOwner=%s, newOwner=%s\n",
                   busName,
                   previousOwner ? previousOwner : "<none>",
                   newOwner ? newOwner : "<none>");
        }
    }

    bool AcceptSessionJoiner(SessionPort sessionPort, const char* joiner, const SessionOpts& opts)
    {
        if (sessionPort != SERVICE_PORT) {
            printf("Rejecting join attempt on unexpected session port %d\n", sessionPort);
            return false;
        }
        printf("Accepting join session request from %s (opts.proximity=%x, opts.traffic=%x, opts.transports=%x)\n",
               joiner, opts.proximity, opts.traffic, opts.transports);
        return true;
    }
};

static BasicSampleObject* testObj = NULL;

static MyBusListener s_busListener;

/** Advertise the service name, report the result to stdout, and return the status code. */
QStatus AdvertiseName(TransportMask mask)
{
    QStatus status = s_msgBus->AdvertiseName(SERVICE_NAME, mask);

    if (ER_OK == status) {
        printf("Advertisement of the service name '%s' succeeded.\n", SERVICE_NAME);
    } else {
        printf("Failed to advertise name '%s' (%s).\n", SERVICE_NAME, QCC_StatusText(status));
    }

    return status;
}

/** Wait for SIGINT before continuing. */
void WaitForSigInt(void)
{
    while (s_interrupt == false) {
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100 * 1000);
#endif
    }
}

class HumanSensor
{
private:
	static const int pin_22 = 6;       // rsp board pin:22
	static void sig(void)
	{
		printf("sig: %d \n", digitalRead(pin_22));

		const char* newName = digitalRead(pin_22) ? "true" : "false";
		string str (newName);

		ajn::MsgArg* msg = new MsgArg(ALLJOYN_STRING);
		msg->v_string.str = str.c_str();
		msg->v_string.len = str.length();

		testObj->Set(INTERFACE_NAME, "name", *msg);
	}
public:
	static int initialize()
	{
		if (wiringPiSetup() == -1)
			return 1;

		pinMode(pin_22, INPUT);
		pullUpDnControl(pin_22, PUD_UP);
		wiringPiISR(pin_22, INT_EDGE_BOTH, &HumanSensor::sig);

		return 0;
	}
};

/** Main entry point */
int CDECL_CALL main(int argc, char** argv, char** envArg)
{
    QCC_UNUSED(argc);
    QCC_UNUSED(argv);
    QCC_UNUSED(envArg);

    if (AllJoynInit() != ER_OK) {
        return 1;
    }

    if (AllJoynRouterInit() != ER_OK) {
        AllJoynShutdown();
        return 1;
    }

    printf("AllJoyn Library version: %s.\n", ajn::GetVersion());
    printf("AllJoyn Library build info: %s.\n", ajn::GetBuildInfo());

    /* Install SIGINT handler */
    signal(SIGINT, SigIntHandler);

    QStatus status = ER_OK;

    /* Create message bus */
    s_msgBus = new BusAttachment("Camera", true);

	s_msgBus->RegisterBusListener(s_busListener);

    s_msgBus->Start();

    testObj = new BasicSampleObject(*s_msgBus, SERVICE_PATH);

    printf("Registering the bus object.\n");
    s_msgBus->RegisterBusObject(*testObj);

    s_msgBus->Connect();

    /*
     * Advertise this service on the bus.
     * There are three steps to advertising this service on the bus.
     * 1) Request a well-known name that will be used by the client to discover
     *    this service.
     * 2) Create a session.
     * 3) Advertise the well-known name.
     */
    const uint32_t flags = DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE;
    s_msgBus->RequestName(SERVICE_NAME, flags);

    const TransportMask SERVICE_TRANSPORT_TYPE = TRANSPORT_ANY;

    SessionOpts opts(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, SERVICE_TRANSPORT_TYPE);
    SessionPort sp = SERVICE_PORT;
    s_msgBus->BindSessionPort(sp, opts, s_busListener);

    s_msgBus->AdvertiseName(SERVICE_NAME, SERVICE_TRANSPORT_TYPE);

	HumanSensor::initialize();

    /* Perform the service asynchronously until the user signals for an exit. */
    if (ER_OK == status) {
        WaitForSigInt();
    }

    /* Clean up */
    delete s_msgBus;
    s_msgBus = NULL;
    delete testObj;
    testObj = NULL;

    printf("Signal service exiting with status 0x%04x (%s).\n", status, QCC_StatusText(status));

    AllJoynRouterShutdown();
    AllJoynShutdown();
    return (int) status;
}
