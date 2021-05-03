// A basic test implementation of Netcore for IPC in Dolphin

#pragma warning(disable : 4564)


#include <string>

#include "VanguardClient.h"
#include "VanguardClientInitializer.h"
#include "ThreadLocalHelper.h"
#include "Helpers.hpp"
//#include "emumem.h"
//#include "addrmap.h"
//#include "emupal.h"
//#include "memarray.h"
//#include "mem.h"
//#include "dos_inc.h"
//#include "dos_system.h"
////#include "sdlmain.h"
//#include <cpu.h>
//#include <mixer.h>
//#include <vga.h>
//#include <thread>
//#include <setup.h>
//#include "control.h"
#include <msclr/marshal_cppstd.h>
//#include <src/hardware/vga_memory.cpp>
#include "UnmanagedWrapper.h"
//#include <src/hardware/mame/emu.h>
#include "VanguardSettingsWrapper.h"
#include "ManagedWrapper.h"
//#include <emucore.h>
//#include <emumem_hem.h>
//#include <include/paging.h>
//#include <src\libs\tinyfiledialogs\tinyfiledialogs.h>
//#include <sdlmain.h>

//#include "core/core.h"
#using < system.dll>
#using < system.windows.forms.dll>
#using <system.collections.dll>
#using <System.IO.Compression.FileSystem.dll>

//If we provide just the dll name and then compile with /AI it works, but intellisense doesn't pick up on it, so we use a full relative path
#using <../../../../../../RTCV/Build/NetCore.dll>
#using <../../../../../../RTCV/Build/Vanguard.dll>
#using <../../../../../../RTCV/Build/CorruptCore.dll>
#using <../../../../../../RTCV/Build/RTCV.Common.dll>


using namespace cli;
using namespace System;
using namespace Text;
using namespace RTCV;
using namespace RTCV::CorruptCore::Extensions;
using namespace NetCore;
using namespace CorruptCore;
using namespace Vanguard;
using namespace Runtime::InteropServices;
using namespace Threading;
using namespace Collections::Generic;
using namespace Reflection;
using namespace Diagnostics;

#define SRAM_SIZE 25165824
#define ARAM_SIZE 16777216
#define EXRAM_SIZE 67108864

//static void EmuThreadExecute(Action^ callback);
//static void EmuThreadExecute(IntPtr ptr);
static int currentMD = 0;
int loadromcounter = 1;
int corestepcounter = 0;
clock_t current_ticks, delta_ticks;
clock_t stepspersecond = 0;
clock_t avgstepspersecond = 0;
// Define this in here as it's managed and weird stuff happens if it's in a header
public
ref class VanguardClient {
public:
    //static ThreadLocal^ localthread;
    static NetCoreReceiver^ receiver;
    static VanguardConnector^ connector;

    static void OnMessageReceived(Object^ sender, NetCoreEventArgs^ e);
    static void SpecUpdated(Object^ sender, SpecUpdateEventArgs^ e);
    static void RegisterVanguardSpec();

    static void StartClient();
    static void RestartClient();
    static void StopClient();

    static void LoadRom(String^ filename, std::string gamename);
    static bool LoadState(std::string filename);
    static bool SaveState(String^ filename, bool wait);

    static String^ GetConfigAsJson(VanguardSettingsWrapper^ settings);
    static VanguardSettingsWrapper^ GetConfigFromJson(String^ json);

    static void LoadWindowPosition();
    static void SaveWindowPosition();
    static String^ GetSyncSettings();
    static void SetSyncSettings(String^ ss);
    //static bool RefreshDomains(bool updateSpecs = true);
    static String^ emuDir = IO::Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location);
    static String^ logPath = IO::Path::Combine(emuDir, "EMU_LOG.txt");

    static cli::array<String^>^ configPaths;

    static volatile bool loading = false;
    static volatile bool stateLoading = false;
    static bool attached = false;
    static Object^ GenericLockObject = gcnew Object();
    static bool enableRTC = true;
    static System::String^ lastStateName = "";
    static System::String^ fileToCopy = "";
    static bool DriveLoaded = false;
    //static Core::TimingEventType* event;
    
};

//static void EmuThreadExecute(Action^ callback) {
//    EmuThreadExecute(Marshal::GetFunctionPointerForDelegate(callback));
//}
//
//static void EmuThreadExecute(IntPtr callbackPtr) {
//    //SetEmuThread(false);
//    //sdl.active = false;
//	ManagedWrapper::Resume(false);
//    static_cast<void(__stdcall*)(void)>(callbackPtr.ToPointer())();
//	ManagedWrapper::Resume(true);
//    //SetEmuThread(true);
//    //sdl.active = true;
//}

static PartialSpec^
getDefaultPartial() {
    PartialSpec^ partial = gcnew PartialSpec("VanguardSpec");
    partial->Set(VSPEC::NAME, "MAME");
    partial->Set(VSPEC::SUPPORTS_RENDERING, true);
    partial->Set(VSPEC::SUPPORTS_CONFIG_MANAGEMENT, false);
    partial->Set(VSPEC::SUPPORTS_CONFIG_HANDOFF, true);
    partial->Set(VSPEC::SUPPORTS_KILLSWITCH, true);
    partial->Set(VSPEC::SUPPORTS_REALTIME, true);
    partial->Set(VSPEC::SUPPORTS_SAVESTATES, true);
    partial->Set(VSPEC::SUPPORTS_REFERENCES, true);
    //partial->Set(VSPEC::REPLACE_MANUALBLAST_WITH_GHCORRUPT, true);
    partial->Set(VSPEC::SUPPORTS_MIXED_STOCKPILE, true);
    partial->Set(VSPEC::CONFIG_PATHS, VanguardClient::configPaths);
    partial->Set(VSPEC::SYSTEM, String::Empty);
    partial->Set(VSPEC::GAMENAME, String::Empty);
    partial->Set(VSPEC::SYSTEMPREFIX, String::Empty);
    partial->Set(VSPEC::OPENROMFILENAME, "");
    partial->Set(VSPEC::OVERRIDE_DEFAULTMAXINTENSITY, 100000);
    partial->Set(VSPEC::SYNCSETTINGS, String::Empty);
	partial->Set(VSPEC::MEMORYDOMAINS_BLACKLISTEDDOMAINS, gcnew cli::array<String^>{});
    partial->Set(VSPEC::SYSTEM, String::Empty);
    partial->Set(VSPEC::LOADSTATE_USES_CALLBACKS, true);
    partial->Set(VSPEC::EMUDIR, VanguardClient::emuDir);
    //partial->Set(VSPEC::SUPPORTS_MULTITHREAD, true);
    return partial;
}

static bool VanguardInitializationComplete = false;
void VanguardClient::SpecUpdated(Object^ sender, SpecUpdateEventArgs^ e) {
    PartialSpec^ partial = e->partialSpec;

    LocalNetCoreRouter::Route(Endpoints::CorruptCore,
        Commands::Remote::PushVanguardSpecUpdate, partial, true);
    LocalNetCoreRouter::Route(Endpoints::UI, Commands::Remote::PushVanguardSpecUpdate,
        partial, true);
}

void VanguardClient::RegisterVanguardSpec() {
    PartialSpec^ emuSpecTemplate = gcnew PartialSpec("VanguardSpec");
    emuSpecTemplate->Insert(getDefaultPartial());

    AllSpec::VanguardSpec = gcnew FullSpec(emuSpecTemplate, true);
    // You have to feed a partial spec as a template

    if(VanguardClient::attached)
        RTCV::Vanguard::VanguardConnector::PushVanguardSpecRef(AllSpec::VanguardSpec);

    LocalNetCoreRouter::Route(Endpoints::CorruptCore,
        Commands::Remote::PushVanguardSpec, emuSpecTemplate, true);
    LocalNetCoreRouter::Route(Endpoints::UI, Commands::Remote::PushVanguardSpec,
        emuSpecTemplate, true);
    AllSpec::VanguardSpec->SpecUpdated += gcnew EventHandler<SpecUpdateEventArgs^>(
        &VanguardClient::SpecUpdated);
}

// Lifted from Bizhawk
static Assembly^ CurrentDomain_AssemblyResolve(Object^ sender, ResolveEventArgs^ args) {
    try {
        Trace::WriteLine("Entering AssemblyResolve\n" + args->Name + "\n" +
            args->RequestingAssembly);
        String^ requested = args->Name;
        Monitor::Enter(AppDomain::CurrentDomain);
        {
            cli::array<Assembly^>^ asms = AppDomain::CurrentDomain->GetAssemblies();
            for(int i = 0; i < asms->Length; i++) {
                Assembly^ a = asms[i];
                if(a->FullName == requested) {
                    return a;
                }
            }

            AssemblyName^ n = gcnew AssemblyName(requested);
            // load missing assemblies by trying to find them in the dll directory
            String^ dllname = n->Name + ".dll";
            String^ directory = IO::Path::Combine(
                IO::Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location), "..",
                "RTCV");
            String^ fname = IO::Path::Combine(directory, dllname);
            if(!IO::File::Exists(fname)) {
                Trace::WriteLine(fname + " doesn't exist");
                return nullptr;
            }

            // it is important that we use LoadFile here and not load from a byte array; otherwise
            // mixed (managed/unamanged) assemblies can't load
            Trace::WriteLine("Loading " + fname);
            return Assembly::UnsafeLoadFrom(fname);
        }
    }
    catch(Exception^ e) {
        Trace::WriteLine("Something went really wrong in AssemblyResolve. Send this to the devs\n" +
            e);
        return nullptr;
    }
    finally {
        Monitor::Exit(AppDomain::CurrentDomain);
    }
}

// Create our VanguardClient
void VanguardClientInitializer::Initialize() {
	//VanguardClientUnmanaged::LOAD_GAME_DONE();
	if (VanguardInitializationComplete == true) return;
    // This has to be in its own method where no other dlls are used so the JIT can compile it
    AppDomain::CurrentDomain->AssemblyResolve +=
        gcnew ResolveEventHandler(CurrentDomain_AssemblyResolve);

    ConfigureVisualStyles();

    StartVanguardClient();
}

//This ensures things render as we want them.
//There are no issues running this within QT/WXWidgets applications
//This HAS to be its own method for the JIT. If it's merged with StartVanguardClient(), fun exceptions occur
void VanguardClientInitializer::ConfigureVisualStyles()
{
    // this needs to be done before the warnings/errors show up
    System::Windows::Forms::Application::EnableVisualStyles();
    System::Windows::Forms::Application::SetCompatibleTextRenderingDefault(false);
}

// Create our VanguardClient
void VanguardClientInitializer::StartVanguardClient()
{
    System::Windows::Forms::Form^ dummy = gcnew System::Windows::Forms::Form();
    IntPtr Handle = dummy->Handle;
    SyncObjectSingleton::SyncObject = dummy;
    //SyncObjectSingleton::EmuInvokeDelegate = gcnew SyncObjectSingleton::ActionDelegate(&EmuThreadExecute);
    SyncObjectSingleton::UseQueue = true;

    // Start everything
    VanguardClient::configPaths = gcnew cli::array<String^>{""
    };

    VanguardClient::StartClient();
    VanguardClient::RegisterVanguardSpec();
    RtcCore::StartEmuSide();

    // Lie if we're in attached
    if(VanguardClient::attached)
        VanguardConnector::ImplyClientConnected();

    //VanguardClient::LoadWindowPosition();
}

void VanguardClient::StartClient() {
    RTCV::Common::Logging::StartLogging(logPath);
	
    // Can't use contains
    auto args = Environment::GetCommandLineArgs();
    for(int i = 0; i < args->Length; i++) {
        if(args[i] == "-ATTACHED") {
            attached = true;
        }
        if(args[i] == "-DISABLERTC") {
            enableRTC = false;
        }
    }
    //RefreshDomains();

    receiver = gcnew NetCoreReceiver();
    receiver->Attached = attached;
    receiver->MessageReceived += gcnew EventHandler<NetCoreEventArgs^>(
        &VanguardClient::OnMessageReceived);
    connector = gcnew VanguardConnector(receiver);
}


void VanguardClient::RestartClient() {
    VanguardClient::StopClient();
    VanguardClient::StartClient();
}

void VanguardClient::StopClient() {
    connector->Kill();
    connector = nullptr;
}

void VanguardClient::LoadWindowPosition() {
    if(connector == nullptr)
        return;
}

void VanguardClient::SaveWindowPosition() {
}

String^ VanguardClient::GetSyncSettings() {
    auto wrapper = VanguardSettingsWrapper::GetVanguardSettingsFromDOSBox();
    auto ws = GetConfigAsJson(wrapper);
    return ws;
}

void VanguardClient::SetSyncSettings(String^ ss) {
    VanguardSettingsWrapper^ wrapper = nullptr;
    //Hack for now to maintain compatibility.
    if(ss == "DOS") {
        wrapper = gcnew VanguardSettingsWrapper();
        //wrapper->is_new_3ds = true;
    }
    else {
        wrapper = GetConfigFromJson(ss);
    }
    VanguardSettingsWrapper::SetSettingsFromWrapper(wrapper);
}

#pragma region MemoryDomains

//For some reason if we do these in another class, melon won't build
public
ref class MAMEMemoryDomain : RTCV::CorruptCore::IMemoryDomain {
public:
	int domainNumber;
	property System::String^ Name { virtual System::String^ get(); virtual void set(System::String^ name); }
	MAMEMemoryDomain(int i)
	{
		domainNumber = i;
	}
	property long long Size { virtual long long get(); }
	property int WordSize { virtual int get(); }
	property bool BigEndian { virtual bool get(); }
	virtual int VerifyNumber(int i);
	virtual unsigned char PeekByte(long long addr);
	virtual cli::array<unsigned char>^ PeekBytes(long long address, int length);
	virtual void PokeByte(long long addr, unsigned char val);
	property std::string MemoryClass {virtual std::string get(); }
	virtual String^ ToString() override
	{
		if (MemoryClass == "Space") return (Helpers::utf8StringToSystemString(ManagedWrapper::GetDeviceName(domainNumber)) + ":" + Helpers::utf8StringToSystemString(MemoryClass) + ":" + Name + "");
		else if (MemoryClass == "Region")
			return (Helpers::utf8StringToSystemString(MemoryClass) + ":" + Name + "_RO");
		else return (Helpers::utf8StringToSystemString(MemoryClass)+":"+ Name + "");
	}

	
};

delegate void MessageDelegate(Object^);
#pragma region MAMEMemoryDomain
String^ MAMEMemoryDomain::Name::get() {/*
    if(UnmanagedWrapper::IS_N3DS()) {
        return "FCRam(N3DS)";
    }
    else {
        return "FCRam";
    }*/
    return Helpers::utf8StringToSystemString(ManagedWrapper::GetMemoryDomain(domainNumber));
}
int MAMEMemoryDomain::VerifyNumber(int i)
{
	return i;
}
void MAMEMemoryDomain::Name::set(System::String^ name) {/*
	if(UnmanagedWrapper::IS_N3DS()) {
		return "FCRam(N3DS)";
	}
	else {
		return "FCRam";
	}*/
	Name = name;
}
std::string MAMEMemoryDomain::MemoryClass::get()
{
	return ManagedWrapper::GetDomainClass(domainNumber);
}
long long MAMEMemoryDomain::Size::get() {/*
    if(UnmanagedWrapper::IS_N3DS()) {
        return MAMEMemoryDomain::FCRAM_N3DS_SIZE;
    }
    return MAMEMemoryDomain::FCRAM_SIZE;*/
    //Section_prop* section = static_cast<Section_prop*>(control->GetSection("dosbox"));
    //Bitu memsizekb = (Bitu)section->Get_int("memsizekb");
    //Bitu memsize = (Bitu)section->Get_int("memsize");

    //if(memsizekb == 0 && memsize < 1) memsize = 1;
    //else if(memsizekb != 0 && (Bits)memsize < 0) memsize = 0;

    ///* round up memsizekb to 4KB multiple */
    //memsizekb = (memsizekb + 3ul) & (~3ul);

    ///* roll memsize into memsizekb, simplify this code */
    //return (memsizekb/1024 + memsize) * 1024ul * 1024ul;
	return ManagedWrapper::GetMemorySize(MemoryClass , Helpers::systemStringToUtf8String(Name), ManagedWrapper::GetCPUSpaceNumber(domainNumber), ManagedWrapper::GetDeviceName(domainNumber));
}

int MAMEMemoryDomain::WordSize::get() {
	return ManagedWrapper::GetByteWidth(MemoryClass, Helpers::systemStringToUtf8String(Name), ManagedWrapper::GetCPUSpaceNumber(domainNumber), ManagedWrapper::GetDeviceName(domainNumber));
}

bool MAMEMemoryDomain::BigEndian::get() {
	/*if (&memory_region::endianness == ENDIANNESS_BIG) {
		return true;
	}
	else */return ManagedWrapper::IsBigEndian(MemoryClass, Helpers::systemStringToUtf8String(Name), ManagedWrapper::GetCPUSpaceNumber(domainNumber), ManagedWrapper::GetDeviceName(domainNumber));
}


unsigned char MAMEMemoryDomain::PeekByte(long long addr) {
    if(addr < MAMEMemoryDomain::Size)
    {
		return ManagedWrapper::PEEK(MemoryClass, Helpers::systemStringToUtf8String(Name), addr, ManagedWrapper::GetCPUSpaceNumber(domainNumber), ManagedWrapper::GetDeviceName(domainNumber));
    }
    else
    {
        printf("Error : the provided address is bigger than the memory size!");
        return 0;
    }
}

void MAMEMemoryDomain::PokeByte(long long addr, unsigned char val) {
    if(addr < MAMEMemoryDomain::Size)
    {
		ManagedWrapper::POKE(MemoryClass, Helpers::systemStringToUtf8String(Name), addr, val, ManagedWrapper::GetCPUSpaceNumber(domainNumber), ManagedWrapper::GetDeviceName(domainNumber));
    }
    else
    {
        printf("Error : the provided address is bigger than the memory size!");
        return;
    }
}

cli::array<unsigned char>^ MAMEMemoryDomain::PeekBytes(long long address, int length) {

    cli::array<unsigned char>^ bytes = gcnew cli::array<unsigned char>(length);
    for(int i = 0; i < length; i++) {
        bytes[i] = PeekByte(address + i);
    }
    return bytes;
}
#pragma endregion

static cli::array<MemoryDomainProxy^>^ GetInterfaces() {
	currentMD = 0;
    if(String::IsNullOrWhiteSpace(AllSpec::VanguardSpec->Get<String^>(VSPEC::OPENROMFILENAME)))
        return gcnew cli::array<MemoryDomainProxy^>(0);
    cli::array<MemoryDomainProxy^>^ interfaces = gcnew cli::array<MemoryDomainProxy^>(ManagedWrapper::GetTotalNumOfRegionsAndShares());
	
	printf("Total Memory Domains: %i\n", ManagedWrapper::GetTotalNumOfRegionsAndShares());
	cli::array <MAMEMemoryDomain^>^ md = gcnew cli::array<MAMEMemoryDomain^>(ManagedWrapper::GetTotalNumOfRegionsAndShares());
	PartialSpec^ spec = gcnew PartialSpec("VanguardSpec");
	cli::array <String^>^ BlacklistDomains = gcnew cli::array<String^>(255);
	for (int i = 0; i < ManagedWrapper::GetTotalNumOfRegionsAndShares(); i++)
	{
		currentMD += 1;
		printf("Current MD Number: %i\n", i);
		md[i] = gcnew MAMEMemoryDomain(i + 1);
		printf("Memory Domain Name: %s\n", Helpers::systemStringToUtf8String(md[i]->Name).c_str());
		//md->ToString()->Remove(0);
		//md->ToString()->Insert(0, md->Name);
		if (!String::IsNullOrWhiteSpace(md[i]->Name))
		{
			if (md[i]->MemoryClass.find("Region") != std::string::npos)
			{
				BlacklistDomains[i] = md[i]->Name;
			}
			printf("Memory Domain Class Name: %s\n", Helpers::systemStringToUtf8String(md[i]->ToString()).c_str());
			interfaces[i] = (gcnew MemoryDomainProxy(md[i]));
		}
	}
	spec->Set(VSPEC::MEMORYDOMAINS_BLACKLISTEDDOMAINS, BlacklistDomains);
    //interfaces[1] = (gcnew MemoryDomainProxy(gcnew screen));
    return interfaces;
}

static bool RefreshDomains(bool updateSpecs = true) {
	currentMD = 0;
    cli::array<MemoryDomainProxy^>^ oldInterfaces = AllSpec::VanguardSpec->Get<cli::array<MemoryDomainProxy^>^>(VSPEC::MEMORYDOMAINS_INTERFACES);
	
    cli::array<MemoryDomainProxy^>^ newInterfaces = GetInterfaces();
	
    // Bruteforce it since domains can change inconsistently in some configs and we keep code
    // consistent between implementations
    bool domainsChanged = false;
    if(oldInterfaces == nullptr)
        domainsChanged = true;
    else {
        domainsChanged = oldInterfaces->Length != newInterfaces->Length;
        for(int i = 0; i < oldInterfaces->Length; i++) {
            if(domainsChanged)
                break;
            if(oldInterfaces[i]->Name != newInterfaces[i]->Name)
                domainsChanged = true;
            if(oldInterfaces[i]->Size != newInterfaces[i]->Size)
                domainsChanged = true;
        }
    }

    if(updateSpecs) {
        AllSpec::VanguardSpec->Update(VSPEC::MEMORYDOMAINS_INTERFACES, newInterfaces, true, true);
        LocalNetCoreRouter::Route(Endpoints::CorruptCore,
            Commands::Remote::EventDomainsUpdated, domainsChanged,
            true);
    }

    return domainsChanged;
}

#pragma endregion

String^ VanguardClient::GetConfigAsJson(VanguardSettingsWrapper^ settings) {
    return JsonHelper::Serialize(settings);
}

VanguardSettingsWrapper^ VanguardClient::GetConfigFromJson(String^ str) {
    return JsonHelper::Deserialize<VanguardSettingsWrapper^>(str);
}

static void STEP_CORRUPT() // errors trapped by CPU_STEP
{
    if(!VanguardClient::enableRTC)
        return;
    RtcClock::StepCorrupt(true, true);
}


#pragma region Hooks
void VanguardClientUnmanaged::CORE_STEP() {
    if(!VanguardInitializationComplete)
        return;
	//thanks https://stackoverflow.com/questions/28530798/how-to-make-a-basic-fps-counter
	current_ticks = clock();
    // Any step hook for corruption
    ActionDistributor::Execute("ACTION");
    STEP_CORRUPT();
	corestepcounter += 1;
	delta_ticks = clock() - current_ticks;
	if (delta_ticks > 0)
	{
		stepspersecond = CLOCKS_PER_SEC / delta_ticks;
		avgstepspersecond = (corestepcounter / current_ticks)/CLOCKS_PER_SEC;
	}
	printf("CORE_STEPs Per Second: %d | CORE_STEPs So Far: %d\n", stepspersecond, corestepcounter);
}

// This is on the main thread not the emu thread
void VanguardClientUnmanaged::LOAD_GAME_START(std::string romPath, std::string fullgamename) {
    if(!VanguardClient::enableRTC)
        return;
	if (!VanguardInitializationComplete)
		return;
	//RefreshDomains();
    //StepActions::ClearStepBlastUnits();	
    LocalNetCoreRouter::Route(Endpoints::CorruptCore, Commands::Remote::ClearStepBlastUnits, false);

    RtcClock::ResetCount();

	VanguardClientUnmanaged::MAME_SAVEROM(romPath);
	VanguardClientUnmanaged::LOAD_GAME_DONE();
}


void VanguardClientUnmanaged::LOAD_GAME_DONE() {

	currentMD = 0;
	if (ManagedWrapper::GetGameName() == "" || ManagedWrapper::GetGameName() == " ")
		return;
	if (VanguardInitializationComplete == false)
		return;
    //RTCV::CorruptCore::StepActions::ClearStepBlastUnits();
    LocalNetCoreRouter::Route(Endpoints::CorruptCore, Commands::Remote::ClearStepBlastUnits, false);
    //This should make blast units go away when a program changes
    //Side effect: for games that use multiple exes, like Ultima 4, the blast units would go away when the game's exe switches to another one.
    if(!VanguardClient::enableRTC)
        return;
    PartialSpec^ gameDone = gcnew PartialSpec("VanguardSpec");

    try {
        gameDone->Set(VSPEC::SYSTEM, "Arcade");
        gameDone->Set(VSPEC::SYSTEMPREFIX, "Arcade");
        gameDone->Set(VSPEC::SYSTEMCORE, "MAME");
        gameDone->Set(VSPEC::CORE_DISKBASED, false);

        String^ oldGame = AllSpec::VanguardSpec->Get<String^>(VSPEC::GAMENAME);

        String^ gameName = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_GETGAMENAME());
        char replaceChar = L'-';
        gameDone->Set(VSPEC::GAMENAME,
            StringExtensions::MakeSafeFilename(gameName, replaceChar));

        //String^ syncsettings = VanguardClient::GetConfigAsJson(VanguardSettingsWrapper::GetVanguardSettingsFromDOSBox());
        //gameDone->Set(VSPEC::SYNCSETTINGS, syncsettings);

        AllSpec::VanguardSpec->Update(gameDone, true, false);
        RefreshDomains();

        //bool domainsChanged = RefreshDomains(true);

        if(oldGame != gameName) {
            LocalNetCoreRouter::Route(Endpoints::UI,
                Commands::Basic::ResetGameProtectionIfRunning, true);
        }

        //RTCV::CorruptCore::StepActions::ClearStepBlastUnits();
		RTCV::NetCore::LocalNetCoreRouter::Route(Endpoints::CorruptCore, NetCore::Commands::Remote::ClearStepBlastUnits, nullptr , false);

    }
    catch(Exception^ e) {
        Trace::WriteLine(e->ToString());
    }

    /*
    VanguardClient::event = Core::System::GetInstance().CoreTiming().RegisterEvent(
        "RTCV::run_event",
        [](u64 thread_id,
               s64 cycle_late)
        {
            RunCallback(
                thread_id, cycle_late);
        });

     Core::System::GetInstance().CoreTiming().ScheduleEvent(run_interval_ticks, VanguardClient::event);
     */
    VanguardClient::loading = false;
    RtcCore::InvokeLoadGameDone();
}


void VanguardClientUnmanaged::LOAD_STATE_DONE() {
    if(!VanguardClient::enableRTC)
        return;
    VanguardClient::stateLoading = false;
}

void VanguardClientUnmanaged::GAME_CLOSED() {
	if (VanguardInitializationComplete == false)
		return;
    if(!VanguardClient::enableRTC)
        return;
	currentMD = 0;
	RefreshDomains();
    AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, String::Empty, true, true);
    RtcCore::InvokeGameClosed(true);
}


bool VanguardClientUnmanaged::RTC_OSD_ENABLED() {
    RefreshDomains();
    if(!VanguardClient::enableRTC)
        return true;
    if(RTCV::NetCore::Params::IsParamSet(RTCSPEC::CORE_EMULATOROSDDISABLED))
        return false;
    return true;
}

#pragma endregion

/*ENUMS FOR THE SWITCH STATEMENT*/
enum COMMANDS {
    SAVESAVESTATE,
    LOADSAVESTATE,
    REMOTE_LOADROM,
    REMOTE_CLOSEGAME,
    REMOTE_DOMAIN_GETDOMAINS,
    REMOTE_KEY_SETSYNCSETTINGS,
    REMOTE_KEY_SETSYSTEMCORE,
    REMOTE_EVENT_EMU_MAINFORM_CLOSE,
    REMOTE_EVENT_EMUSTARTED,
    REMOTE_ISNORMALADVANCE,
    REMOTE_EVENT_CLOSEEMULATOR,
    REMOTE_ALLSPECSSENT,
    REMOTE_POSTCORRUPTACTION,
	REMOTE_RESUMEEMULATION,
	REMOTE_RENDERSTART,
	REMOTE_RENDERSTOP,
    UNKNOWN
};

inline COMMANDS CheckCommand(String^ inString) {
    if(inString == RTCV::NetCore::Commands::Basic::LoadSavestate)
        return LOADSAVESTATE;
    else if(inString == RTCV::NetCore::Commands::Basic::SaveSavestate)
        return SAVESAVESTATE;
    else if(inString == RTCV::NetCore::Commands::Remote::LoadROM)
        return REMOTE_LOADROM;
    else if(inString == RTCV::NetCore::Commands::Remote::CloseGame)
        return REMOTE_CLOSEGAME;
    else if(inString == RTCV::NetCore::Commands::Remote::AllSpecSent)
        return REMOTE_ALLSPECSSENT;
    else if(inString == RTCV::NetCore::Commands::Remote::DomainGetDomains)
        return REMOTE_DOMAIN_GETDOMAINS;
    else if(inString == RTCV::NetCore::Commands::Remote::KeySetSystemCore)
        return REMOTE_KEY_SETSYSTEMCORE;
    else if(inString == RTCV::NetCore::Commands::Remote::KeySetSyncSettings)
        return REMOTE_KEY_SETSYNCSETTINGS;
    else if(inString == RTCV::NetCore::Commands::Remote::EventEmuMainFormClose)
        return REMOTE_EVENT_EMU_MAINFORM_CLOSE;
    else if(inString == RTCV::NetCore::Commands::Remote::EventEmuStarted)
        return REMOTE_EVENT_EMUSTARTED;
    else if(inString == RTCV::NetCore::Commands::Remote::IsNormalAdvance)
        return REMOTE_ISNORMALADVANCE;
    else if(inString == RTCV::NetCore::Commands::Remote::EventCloseEmulator)
        return REMOTE_EVENT_CLOSEEMULATOR;
    else if(inString == RTCV::NetCore::Commands::Remote::EventCloseEmulator)
        return REMOTE_POSTCORRUPTACTION;
    else if(inString == RTCV::NetCore::Commands::Remote::ResumeEmulation)
        return REMOTE_RESUMEEMULATION;
	else if (inString == RTCV::NetCore::Commands::Remote::RenderStart)
		return REMOTE_RENDERSTART;
	else if (inString == RTCV::NetCore::Commands::Remote::RenderStop)
		return REMOTE_RENDERSTOP;

    else return UNKNOWN;
}

/* IMPLEMENT YOUR COMMANDS HERE */
void VanguardClient::LoadRom(String^ filename, std::string gamename) {
    String^ currentOpenRom = "";
    if(AllSpec::VanguardSpec->Get<String^>(VSPEC::OPENROMFILENAME) != "")
        currentOpenRom = AllSpec::VanguardSpec->Get<String^>(VSPEC::OPENROMFILENAME);

    // Game is not running
    if(currentOpenRom != filename) {
        PartialSpec^ partial = gcnew PartialSpec("VanguardSpec");
        partial->Set(VSPEC::OPENROMFILENAME, filename);
        AllSpec::VanguardSpec->Update(partial, true, false);
        std::string path = Helpers::systemStringToUtf8String(filename);
        loading = true;
        //UnmanagedWrapper::VANGUARD_LOADGAME(path);
		ManagedWrapper::LOADGAME(path, gamename);
        RefreshDomains();
        // We have to do it this way to prevent deadlock due to synced calls. It sucks but it's
        // required at the moment
        while(loading) {
            Thread::Sleep(20);
            System::Windows::Forms::Application::DoEvents();
        }

        //Thread::Sleep(10); // Give the emu thread a chance to recover
    }
}


bool VanguardClient::LoadState(std::string filename) {

    //RTCV::CorruptCore::StepActions::ClearStepBlastUnits();
	LocalNetCoreRouter::Route(Endpoints::CorruptCore, Commands::Remote::ClearStepBlastUnits, false);

	//ManagedWrapper::hardreset();
    //control->ParseConfigFile(Helpers::systemStringToUtf8String((Helpers::utf8StringToSystemString(filename) + ".conf")).c_str());
    
    RtcClock::ResetCount();
    stateLoading = true;
	//ManagedWrapper::Resume(false);
	//UnmanagedWrapper::VANGUARD_LOADSTATE_DONE();
    //// We have to do it this way to prevent deadlock due to synced calls. It sucks but it's required
    //// at the moment
    ////int i = 0;
    //do {
    //    Thread::Sleep(20);
    //    System::Windows::Forms::Application::DoEvents();

    //    // We wait for 20 ms every time. If loading a game takes longer than 10 seconds, break out.
    //    /*if(++i > 500) {
    //        stateLoading = false;
    //        return false;
    //    }*/
    //} while(stateLoading);
	ManagedWrapper::LoadSaveState(filename);
	UnmanagedWrapper::VANGUARD_LOADSTATE_DONE();
	//ManagedWrapper::Resume(true);
    RefreshDomains();
    return true;
}

bool VanguardClient::SaveState(String^ filename, bool wait) {
    std::string s = Helpers::systemStringToUtf8String(filename);
    //const char* converted_filename = s.c_str();
    VanguardClient::lastStateName = filename;
    VanguardClient::fileToCopy = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_SAVESTATE(s));
    //IO::FileInfo^ file = gcnew IO::FileInfo((filename)+".conf");
    //if(file->Directory != nullptr && file->Directory->Exists == false)
    //    file->Directory->Create();
    ////control->PrintConfig((Helpers::systemStringToUtf8String((filename)+".conf")).c_str());
    ////LOG_MSG("Savestate filename is %s", VanguardClient::fileToCopy);
    //Thread::Sleep(2000);
    //IO::File::Copy(VanguardClient::fileToCopy, filename);
    VanguardClientUnmanaged::SAVE_STATE_DONE();
    return true;
}

void VanguardClientUnmanaged::SAVE_STATE_DONE() {
    if(!VanguardClient::enableRTC || VanguardClient::fileToCopy == nullptr ||
        VanguardClient::fileToCopy == "")
        return;
    ////Thread::Sleep(2000);
    ////System::IO::File::Copy(VanguardClient::fileToCopy, VanguardClient::lastStateName, true);

    String^ gameName = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_GETGAMENAME());

    ////force update current game after savestate until we can detect an actual game change
    ////TODO: Replace this with proper handling
    PartialSpec^ gameDone = gcnew PartialSpec("VanguardSpec");
    char replaceChar = L'-';
    gameDone->Set(VSPEC::GAMENAME,
        StringExtensions::MakeSafeFilename(gameName, replaceChar));
    AllSpec::VanguardSpec->Update(gameDone, true, false);
}

//void VanguardClientUnmanaged::MAME_LOADEXE() {
//
//    if(VanguardClient::DriveLoaded)
//    {
//        MessageBox(GetHWND(), "Dosbox-x has to restart before a new drive is loaded", "Restarting Dosbox", MB_OK);
//        System::Environment::Exit(0);
//    }
//
//        char CurrentDir[512];
//        char* Temp_CurrentDir = CurrentDir;
//        getcwd(Temp_CurrentDir, 512);
//        const char* lFilterPatterns[] = { "*.com","*.exe","*.bat","*.COM","*.EXE","*.BAT" };
//        const char* lFilterDescription = "Executable files (*.com, *.exe, *.bat)";
//        char const* lTheOpenFileName = tinyfd_openFileDialog("Select an executable file to launch", "", 6, lFilterPatterns, lFilterDescription, 0);
//
//        if(lTheOpenFileName)
//        {
//            System::IO::FileInfo^ fi = gcnew System::IO::FileInfo(gcnew System::String(lTheOpenFileName));
//            System::IO::DirectoryInfo^ di = fi->Directory;
//            System::String^ autoexec_rom_path = System::IO::Path::Combine(di->FullName, gcnew System::String("autoexec.rom"));
//            System::IO::File::WriteAllText(autoexec_rom_path, fi->Name);
//
//            System::String^ romSessionPath = RTCV::CorruptCore::Drive::PackageDrive(di->FullName);
//
//            System::String^ autoexec = RTCV::CorruptCore::Drive::UnpackageDrive(romSessionPath);
//
//            //thx https://stackoverflow.com/questions/2093331/converting-systemstring-to-const-char
//            msclr::interop::marshal_context oMarshalContext;
//            const char* autoexecPath = oMarshalContext.marshal_as<const char*>(autoexec);
//
//            VanguardClient::DriveLoaded = true;
//            VanguardClient::LoadRom(romSessionPath);
//
//            VanguardClientUnmanaged::LoadExecutable(autoexecPath);
//        }
//}
//
void VanguardClientUnmanaged::MAME_LOADROM() {

    /*if(VanguardClient::DriveLoaded)
    {
        MessageBox(GetHWND(), "Dosbox-x has to restart before a new drive is loaded", "Restarting Dosbox", MB_OK);
        System::Environment::Exit(0);
    }*/

    //char CurrentDir[512];
    //char* Temp_CurrentDir = CurrentDir;
    ////getcwd(Temp_CurrentDir, 512);
    //MAME_LOA

}

void VanguardClientUnmanaged::MAME_LOADROM(char const* lTheOpenFileName) {

    //if(VanguardClient::DriveLoaded)
    //{
    //    MessageBox(GetHWND(), "Dosbox-x has to restart before a new drive is loaded", "Restarting Dosbox", MB_OK);
    //    System::Environment::Exit(0);
    //}

    System::IO::FileInfo^ fi = gcnew System::IO::FileInfo(gcnew System::String(lTheOpenFileName));
    RTCV::CorruptCore::Drive::UnpackageDrive(fi->FullName);
	System::String^ autoexec = System::IO::Path::Combine(RtcCore::workingDir, "DRIVE", "autoexec.rom");
	System::String^ shortgamename = System::IO::File::ReadAllText(autoexec);
	System::String^ autoexecdir = System::IO::Path::Combine(RtcCore::workingDir, "DRIVE");
    //    //thx https://stackoverflow.com/questions/2093331/converting-systemstring-to-const-char
    //    msclr::interop::marshal_context oMarshalContext;
    //    const char* autoexecPath = oMarshalContext.marshal_as<const char*>(autoexec);

    //    VanguardClientUnmanaged::LoadRom(fi->FullName);
    //    VanguardClient::DriveLoaded = true;

    //    VanguardClientUnmanaged::LoadExecutable(autoexecPath);
	ManagedWrapper::LOADROM(Helpers::systemStringToUtf8String(autoexecdir), Helpers::systemStringToUtf8String(shortgamename));
        
 
}

void VanguardClientUnmanaged::MAME_SAVEROM(std::string rompath) {

	System::String^ romSessionPath;
    /*if(VanguardClient::DriveLoaded)
    {
        MessageBox(GetHWND(), "Cannot generate a drive while a drive is loaded. Cancelling operation", "Error", MB_OK);
        return;
    }

    System::String^ romSessionPath;

    {

        char CurrentDir[512];
        char* Temp_CurrentDir = CurrentDir;
        getcwd(Temp_CurrentDir, 512);
        const char* lFilterPatterns[] = { "*.com","*.exe","*.bat","*.COM","*.EXE","*.BAT" };
        const char* lFilterDescription = "Executable files (*.com, *.exe, *.bat)";
        char const* lTheOpenFileName = tinyfd_openFileDialog("Select an executable file to launch", "", 6, lFilterPatterns, lFilterDescription, 0);



        if(lTheOpenFileName)
        {
            System::IO::FileInfo^ fi = gcnew System::IO::FileInfo(gcnew System::String(lTheOpenFileName));
            System::IO::DirectoryInfo^ di = fi->Directory;
            System::String^ autoexec_rom_path = System::IO::Path::Combine(di->FullName, gcnew System::String("autoexec.rom"));
            System::IO::File::WriteAllText(autoexec_rom_path, fi->Name);

            romSessionPath = RTCV::CorruptCore::Drive::PackageDrive(di->FullName);

            System::String^ autoexec = RTCV::CorruptCore::Drive::UnpackageDrive(romSessionPath);

        }
        else
        {
            return;
        }



    }


    {
        char CurrentDir[512];
        char* Temp_CurrentDir = CurrentDir;
        getcwd(Temp_CurrentDir, 512);
        const char* lFilterPatterns[] = { "*.drv","*.drive","*.DRV","*.DRIVE" };
        const char* lFilterDescription = "RTC Drive file (*.drv, *.drive)";
        char const* lTheOpenFileName = tinyfd_saveFileDialog("Select a destination for drive file", "", 6, lFilterPatterns, lFilterDescription);

        if(lTheOpenFileName)
        {
            System::String^ path = gcnew System::String(lTheOpenFileName);
            RTCV::CorruptCore::Drive::SaveCurrentDriveAs(path);
        }
    }*/
	//ManagedWrapper::SAVEROM(rompath);
	std::string romfilenames[500];
	cli::array<System::IO::FileInfo^>^ fi = gcnew cli::array<System::IO::FileInfo^> (500);
	cli::array<System::String^>^ finame = gcnew cli::array<System::String^>(500);
	//cli::array<System::IO::FileInfo^>^ destfi;
	std::string destfile;
	std::string destfolder;
	loadromcounter = 0;
	for (loadromcounter = 0; loadromcounter <= ManagedWrapper::COLLECTTOTALROMS(); loadromcounter++)
	{
		if (ManagedWrapper::GETROMFILENAME(loadromcounter) != " ")
		{
			printf("Current filename number: %i\n", loadromcounter);
			romfilenames[loadromcounter] = ManagedWrapper::GETROMFILENAME(loadromcounter);
			printf("Current filename detected: %s\n", romfilenames[loadromcounter].c_str());
			finame[loadromcounter] = Helpers::utf8StringToSystemString(romfilenames[loadromcounter]);
			printf("Converted filename for System::String: %s\n", Helpers::systemStringToUtf8String(finame[loadromcounter]).c_str());
			fi[loadromcounter] = gcnew System::IO::FileInfo(finame[loadromcounter]);
			printf("File opened: %s\n", Helpers::systemStringToUtf8String(fi[loadromcounter]->Name).c_str());
			destfile = Helpers::systemStringToUtf8String(fi[1]->DirectoryName + "\\" + Helpers::utf8StringToSystemString(ManagedWrapper::GetGameName()) + "\\" + fi[loadromcounter]->Name);
			destfolder = Helpers::systemStringToUtf8String(fi[1]->DirectoryName + "\\" + Helpers::utf8StringToSystemString(ManagedWrapper::GetGameName()));
			
			System::IO::Directory::CreateDirectory(Helpers::utf8StringToSystemString(destfolder));
			printf("Destination file for %s: %s\n", Helpers::systemStringToUtf8String(fi[loadromcounter]->Name).c_str(), destfile.c_str());
			if (!System::IO::File::Exists(Helpers::utf8StringToSystemString(destfile)))
			{
				System::IO::File::Copy(fi[loadromcounter]->FullName, Helpers::utf8StringToSystemString(destfile));
					
			}
			
			
		}

	}
	System::IO::FileInfo^ destfi = gcnew System::IO::FileInfo(Helpers::utf8StringToSystemString(destfile));
	System::IO::DirectoryInfo^ di = destfi->Directory;
	System::String^ autoexec_rom_path = System::IO::Path::Combine(di->FullName, gcnew System::String("autoexec.rom"));
	if (!System::IO::File::Exists(autoexec_rom_path))
	{
		System::IO::File::WriteAllText(autoexec_rom_path, Helpers::utf8StringToSystemString(ManagedWrapper::GetShortGameName()));
		romSessionPath = RTCV::CorruptCore::Drive::PackageDrive(di->FullName);
		AllSpec::VanguardSpec->Update(VSPEC::OPENROMFILENAME, romSessionPath, true, true);
		System::IO::Directory::Delete(Helpers::utf8StringToSystemString(destfolder), true);
		//RTCV::CorruptCore::Drive::SaveCurrentDriveAs(romSessionPath);
	}
}

void VanguardClientUnmanaged::FLUSHROMCOUNTER()
{
	loadromcounter = 1;
}


// No fun anonymous classes with closure here
#pragma region Delegates
void StopGame() {
    UnmanagedWrapper::VANGUARD_STOPGAME();
}

void Quit() {
    System::Environment::Exit(0);
}

void AllSpecsSent() {
    VanguardClient::LoadWindowPosition();
    RefreshDomains();
}
#pragma endregion

/* THIS IS WHERE YOU HANDLE ANY RECEIVED MESSAGES */
void VanguardClient::OnMessageReceived(Object^ sender, NetCoreEventArgs^ e) {
    NetCoreMessage^ message = e->message;
    NetCoreAdvancedMessage^ advancedMessage;

    if(Helpers::is<NetCoreAdvancedMessage^>(message))
        advancedMessage = static_cast<NetCoreAdvancedMessage^>(message);

    switch(CheckCommand(message->Type)) {
    case REMOTE_ALLSPECSSENT: {
        auto g = gcnew SyncObjectSingleton::GenericDelegate(&AllSpecsSent);
        SyncObjectSingleton::FormExecute(g);
        VanguardInitializationComplete = true;
    }
    break;

    case LOADSAVESTATE: {
        cli::array<Object^>^ cmd = static_cast<cli::array<Object^>^>(advancedMessage->objectValue);
        String^ path = static_cast<String^>(cmd[0]);
        std::string converted_path = Helpers::systemStringToUtf8String(path);

        // Load up the sync settings
        //String^ settingStr = AllSpec::VanguardSpec->Get<String^>(VSPEC::SYNCSETTINGS);
        /*if(!String::IsNullOrEmpty(settingStr)) {
            VanguardClient::SetSyncSettings(settingStr);
        }*/
        bool success = LoadState(converted_path);
        // bool success = true;
        e->setReturnValue(success);
    }
                      break;

    case SAVESAVESTATE: {
        String^ Key = (String^)(advancedMessage->objectValue);

        //Save the syncsettings
        //AllSpec::VanguardSpec->Set(VSPEC::SYNCSETTINGS, VanguardClient::GetSyncSettings());

        // Build the shortname
        String^ quickSlotName = Key + ".timejump";
        // Get the prefix for the state

        String^ gameName = Helpers::utf8StringToSystemString(UnmanagedWrapper::VANGUARD_GETGAMENAME());

        char replaceChar = L'-';
        String^ prefix = StringExtensions::MakeSafeFilename(gameName, replaceChar);
        prefix = prefix->Substring(prefix->LastIndexOf('\\') + 1);

        String^ path = nullptr;
        // Build up our path
        path = RtcCore::workingDir + IO::Path::DirectorySeparatorChar + "SESSION" + IO::Path::
            DirectorySeparatorChar + prefix + "." + quickSlotName + ".State";

        // If the path doesn't exist, make it
        IO::FileInfo^ file = gcnew IO::FileInfo(path);
        if(file->Directory != nullptr && file->Directory->Exists == false)
            file->Directory->Create();
        VanguardClient::SaveState(path, true);
        e->setReturnValue(path);
    }
                      break;

    case REMOTE_LOADROM: {
        String^ filename = (String^)advancedMessage->objectValue;

        //thx https://stackoverflow.com/questions/2093331/converting-systemstring-to-const-char
        msclr::interop::marshal_context oMarshalContext;
        const char* filenamePath = oMarshalContext.marshal_as<const char*>(filename);

        String^ currentOpenRom = "";
        if(AllSpec::VanguardSpec->Get<String^>(VSPEC::OPENROMFILENAME) != "")
            currentOpenRom = AllSpec::VanguardSpec->Get<String^>(VSPEC::OPENROMFILENAME);

        // Game is not running
        if(currentOpenRom != filename) {
            //System::Action<const char*>^ a = gcnew Action<const char*>(VanguardClientUnmanaged::MAME_LOADROM);
            //SyncObjectSingleton::FormExecute<const char*>(a, filenamePath);

        VanguardClientUnmanaged::MAME_LOADROM(filenamePath);
        }
		//LoadRom(filename, nullptr);
    }
                       break;

    case REMOTE_CLOSEGAME: {
        SyncObjectSingleton::GenericDelegate^ g = gcnew SyncObjectSingleton::GenericDelegate(&StopGame);
        SyncObjectSingleton::FormExecute(g);
    }
                         break;

    case REMOTE_DOMAIN_GETDOMAINS: {
        RefreshDomains();
    }
                                 break;

    case REMOTE_KEY_SETSYNCSETTINGS: {
        String^ settings = (String^)(advancedMessage->objectValue);
        AllSpec::VanguardSpec->Set(VSPEC::SYNCSETTINGS, settings);
    }
                                   break;

    case REMOTE_KEY_SETSYSTEMCORE: {
        // Do nothing
    }
                                 break;

    case REMOTE_EVENT_EMUSTARTED: {
        //SetEmuThread(true);
        // Do nothing
    }
                                break;

    case REMOTE_ISNORMALADVANCE: {
        // Todo - Dig out fast forward?
        e->setReturnValue(true);
    }
                               break;
    case REMOTE_POSTCORRUPTACTION: {

    }
                                 break;

    case REMOTE_RESUMEEMULATION: {
        UnmanagedWrapper::VANGUARD_RESUMEEMULATION();
    }
                               break;
	case REMOTE_RENDERSTART:
	{
		String^ Key = "RENDER_" + (RtcCore::GetRandomKey());
		switch (Render::RenderType)
		{
		case Render::RENDERTYPE::AVI:
		{
			ManagedWrapper::RenderStartAVI(Helpers::systemStringToUtf8String(IO::Path::Combine(RtcCore::RtcDir, "RENDEROUTPUT", Key + ".avi")));
		} break;
		case Render::RENDERTYPE::WAV:
		{
			ManagedWrapper::RenderStartWAV(Helpers::systemStringToUtf8String(IO::Path::Combine(RtcCore::RtcDir, "RENDEROUTPUT", Key + ".wav")));
		} break;
		case Render::RENDERTYPE::MPEG: //instead of mpeg, make an apng
		{
			ManagedWrapper::RenderStartMNG(Helpers::systemStringToUtf8String(IO::Path::Combine(RtcCore::RtcDir, "RENDEROUTPUT", Key + ".png")));
		} break;
		}
	} break;
	case REMOTE_RENDERSTOP:
	{
		switch (Render::RenderType)
		{
		case Render::RENDERTYPE::AVI:
		{
			ManagedWrapper::RenderStopAVI();
		} break;
		case Render::RENDERTYPE::WAV:
		{
			ManagedWrapper::RenderStopWAV();
		} break;
		case Render::RENDERTYPE::MPEG: //instead of mpeg, make an apng
		{
			ManagedWrapper::RenderStopMNG();
		} break;
		}
	} break;
    case REMOTE_EVENT_EMU_MAINFORM_CLOSE:
    case REMOTE_EVENT_CLOSEEMULATOR: {
        //Don't allow re-entry on this
        Monitor::Enter(VanguardClient::GenericLockObject);
        {
            VanguardClient::SaveWindowPosition();
            Quit();
        }
        Monitor::Exit(VanguardClient::GenericLockObject);
    }
                                   break;
	
    default:
        break;
    }
}

VanguardSettingsWrapper^ VanguardSettingsWrapper::GetVanguardSettingsFromDOSBox() {
    VanguardSettingsWrapper^ vSettings = gcnew VanguardSettingsWrapper();

    UnmanagedWrapper::GetSettingsFromDOSBox();
    vSettings->is_new_3ds = UnmanagedWrapper::nSettings.is_new_3ds;
    vSettings->region_value = UnmanagedWrapper::nSettings.region_value;
    //vSettings->init_clock = UnmanagedWrapper::nSettings.init_clock;
    //vSettings->init_time = UnmanagedWrapper::nSettings.init_time;
    vSettings->shaders_accurate_mul = UnmanagedWrapper::nSettings.shaders_accurate_mul;
    vSettings->upright_screen = UnmanagedWrapper::nSettings.upright_screen;
    //vSettings->enable_Mixer_lle = UnmanagedWrapper::nSettings.enable_Mixer_lle;
    //vSettings->enable_Mixer_lle_multithread = UnmanagedWrapper::nSettings.enable_Mixer_lle_multithread;

    // settings->birthmonth = Service::PTM::Module::GetPlayCoins();
    // settings->birthday = Service::PTM::Module::GetPlayCoins();
    // settings->language_index = GetSystemLanguage()
    // settings->country = Service::PTM::Module::GetPlayCoins();
    // settings->play_coin = Service::PTM::Module::GetPlayCoins();

    return vSettings;
}

void VanguardSettingsWrapper::SetSettingsFromWrapper(VanguardSettingsWrapper^ vSettings) {
    UnmanagedWrapper::nSettings.is_new_3ds = vSettings->is_new_3ds;
    UnmanagedWrapper::nSettings.region_value = vSettings->region_value;
    //UnmanagedWrapper::nSettings.init_clock = 1;
    //UnmanagedWrapper::nSettings.init_time = vSettings->init_time;
    UnmanagedWrapper::nSettings.shaders_accurate_mul = vSettings->shaders_accurate_mul;
    UnmanagedWrapper::nSettings.upright_screen = vSettings->upright_screen;
    //UnmanagedWrapper::nSettings.enable_Mixer_lle = vSettings->enable_Mixer_lle;
    //UnmanagedWrapper::nSettings.enable_Mixer_lle_multithread = vSettings->enable_Mixer_lle_multithread;
    UnmanagedWrapper::SetSettingsFromUnmanagedWrapper();
}
