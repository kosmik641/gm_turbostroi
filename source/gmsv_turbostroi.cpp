#include "gmsv_turbostroi.h"
using namespace GarrysMod::Lua;
//------------------------------------------------------------------------------
// Shared thread printing stuff
//------------------------------------------------------------------------------
bool g_ForceThreadsFinished = false; // For correct unrequire module
double g_TargetTime = 0.0;
int g_ThreadTickrate = 10;
int g_SimThreadAffinityMask = 0;
std::vector<train_system> g_MetrostroiSystemList;
std::queue<train_system> g_LoadSystemList;
std::map<std::string, std::string> g_LoadedFilesCache;

//------------------------------------------------------------------------------
// Turbostroi sim thread API
//------------------------------------------------------------------------------
std::queue<shared_message> g_SharedMessagesQueue;
Mutex g_SharedMessagesMutex;
int shared_print(lua_State* L) {
	int n = lua_gettop(L);
	int i;
	char buffer[512];
	char* buf = buffer;
	buffer[0] = 0;

	lua_getglobal(L, "tostring");
	for (i = 1; i <= n; i++) {
		const char* str;
		lua_pushvalue(L, -1);
		lua_pushvalue(L, i);
		lua_call(L, 1, 1);
		str = lua_tostring(L, -1);
		if (strlen(str) + strlen(buffer) < 512) {
			strcpy(buf, str);
			buf = buf + strlen(buf);
			buf[0] = '\t';
			buf = buf + 1;
			buf[0] = 0;
		}
		else if (i == 1 && buffer[0] == 0) {
			strcpy(buf, "[!] Message length limit reached!");
			buf = buf + strlen(buf);
			buf[0] = '\t';
			buf = buf + 1;
			buf[0] = 0;
		}
		lua_pop(L, 1);
	}
	buffer[strlen(buffer) - 1] = '\n';

	shared_message msg;
	char tempbuffer[512] = { 0 };
	strncat(tempbuffer, buffer, (512 - 1) - strlen(buffer));
	strcpy(msg.message, tempbuffer);
	g_SharedMessagesMutex.lock();
	g_SharedMessagesQueue.push(msg);
	g_SharedMessagesMutex.unlock();
	return 0;
}

extern "C" TURBOSTROI_EXPORT bool ThreadSendMessage(void* p, int message, const char* system_name, const char* name, double index, double value) {
	bool successful = false;

	thread_userdata* userdata = (thread_userdata*)p;

	if (userdata) {
		thread_msg tmsg;
		tmsg.message = message;
		strncpy(tmsg.system_name, system_name, 63);
		tmsg.system_name[63] = 0;
		strncpy(tmsg.name, name, 63);
		tmsg.name[63] = 0;
		tmsg.index = index;
		tmsg.value = value;
		userdata->thread_to_sim_mutex.lock();
		userdata->thread_to_sim.push(tmsg);
		userdata->thread_to_sim_mutex.unlock();
		successful = true;
	}
	return successful;
}

int thread_recvmessages(lua_State* state) {
	lua_getglobal(state,"_userdata");
	thread_userdata* userdata = (thread_userdata*)lua_touserdata(state,-1);
	lua_pop(state,1);

	if (userdata) {
		userdata->sim_to_thread_mutex.lock();
		size_t total = userdata->sim_to_thread.size();
		lua_createtable(state, total, 0);
		for (size_t i = 0; i < total; ++i) {
			thread_msg tmsg = userdata->sim_to_thread.front();
			lua_createtable(state, 0, 5);
			lua_pushnumber(state, tmsg.message);     lua_rawseti(state, -2, 1);
			lua_pushstring(state, tmsg.system_name); lua_rawseti(state, -2, 2);
			lua_pushstring(state, tmsg.name);        lua_rawseti(state, -2, 3);
			lua_pushnumber(state, tmsg.index);       lua_rawseti(state, -2, 4);
			lua_pushnumber(state, tmsg.value);       lua_rawseti(state, -2, 5);
			lua_rawseti(state, -2, i);
			userdata->sim_to_thread.pop();
		}
		userdata->sim_to_thread_mutex.unlock();
		return 1;
	}
	return 0;
}

extern "C" TURBOSTROI_EXPORT thread_msg ThreadRecvMessage(void* p) {
	thread_userdata* userdata = (thread_userdata*)p;
	thread_msg tmsg;
	//tmsg.message = NULL;
	if (userdata) {
		userdata->sim_to_thread_mutex.lock();
		tmsg = userdata->sim_to_thread.front();
		userdata->sim_to_thread.pop();
		userdata->sim_to_thread_mutex.unlock();
	}
	return tmsg;
}

extern "C" TURBOSTROI_EXPORT int ThreadReadAvailable(void* p) {
	thread_userdata* userdata = (thread_userdata*)p;

	userdata->sim_to_thread_mutex.lock();
	int n = userdata->sim_to_thread.size();
	userdata->sim_to_thread_mutex.unlock();
	return n;
}

//------------------------------------------------------------------------------
// Turbostroi v2 Logic
//------------------------------------------------------------------------------
void threadSimulation(thread_userdata* userdata) {

	if (g_SimThreadAffinityMask)
	{
#if defined(_WIN32)
		if (!SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(g_SimThreadAffinityMask))) {
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetSTAffinityMask failed on train thread!\n");
		}
#elif defined(POSIX)
		cpu_set_t cpuSet;
		CPU_ZERO(&cpuSet);

		for (int i = 0; i < 32; i++)
			if (g_SimThreadAffinityMask & (1 << i))
				CPU_SET(i, &cpuSet);

		if (sched_setaffinity(getpid(), sizeof(cpuSet), &cpuSet))
			ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: SetSTAffinityMask failed on train thread! (0x%04X)\n",errno);
#endif
	}

	if (userdata == nullptr)
	{
		ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Fail to get thread userdata!\n");
		return;
	}
	
	lua_State* L = userdata->L;
	while (!g_ForceThreadsFinished && userdata && !userdata->finished) {

		lua_pushnumber(L, Plat_FloatTime());
		lua_setglobal(L, "CurrentTime");

		bool needThink = (userdata->current_time < g_TargetTime);
		if (needThink)
			userdata->current_time = g_TargetTime;

		lua_getglobal(L, "Think");
		lua_pushboolean(L, !needThink);
		if (lua_pcall(L, 1, 0, 0)) {
			std::string err = lua_tostring(L, -1);
			err += "\n";
			shared_message msg;
			strcpy(msg.message, err.c_str());
			lua_pop(L, 1);

			g_SharedMessagesMutex.lock();
			g_SharedMessagesQueue.push(msg);
			g_SharedMessagesMutex.unlock();
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(g_ThreadTickrate));
	}

	//Release resources
	lua_pushcfunction(L,shared_print);
	lua_pushstring(L,"[!] Terminating train thread");
	lua_call(L,1,0);
	lua_close(L);
	delete userdata;
}

//------------------------------------------------------------------------------
// Metrostroi Lua API
//------------------------------------------------------------------------------
void load(GarrysMod::Lua::ILuaBase* LUA, lua_State* L, const char* filename, const char* path) {
	//Load up "sv_turbostroi.lua" in the new JIT environment
	const char* file_data = NULL;
	auto cache_item = g_LoadedFilesCache.find(filename);
	if (cache_item != g_LoadedFilesCache.end()) {
		file_data = cache_item->second.c_str();
	}
	else {
		LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
			LUA->GetField(-1, "file");
				LUA->GetField(-1, "Read");
					LUA->PushString(filename);
					LUA->PushString(path);
				LUA->Call(2, 1); //file.Read(...)

				if (LUA->GetString(-1)) {
					file_data = LUA->GetString(-1);
					g_LoadedFilesCache.emplace(filename, file_data);
				}
				LUA->Pop(); //returned result
			LUA->Pop(); //file
		LUA->Pop(); //GLOB
	}

	if (file_data) {
		if (luaL_loadbuffer(L, file_data, strlen(file_data), filename) || lua_pcall(L, 0, LUA_MULTRET, 0)) {
			ConColorMsg(Color(255, 0, 127, 255), "%s\n", lua_tostring(L, -1));
			lua_pop(L, 1);
		}
	} else {
		ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s' in '%s' path)\n", filename, path);
	}
}

LUA_FUNCTION( API_InitializeTrain ) 
{
	//Initialize LuaJIT for train
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);

	lua_pushboolean(L, 1);
	lua_setglobal(L, "TURBOSTROI");
	lua_pushcfunction(L, shared_print);
	lua_setglobal(L, "print");
	lua_pushcfunction(L, thread_recvmessages);
	lua_setglobal(L, "RecvMessages");

	//Load neccessary files
	load(LUA, L, "metrostroi/sv_turbostroi_v2.lua", "LUA");
	load(LUA, L, "metrostroi/sh_failsim.lua", "LUA");

	//Load up all the systems
	for (train_system sys : g_MetrostroiSystemList)
	{
		load(LUA, L, sys.FileName.c_str(), "LUA");
	}

	//Initialize all the systems reported by the train
	int i = 1;
	while (!g_LoadSystemList.empty())
	{
		train_system sys = g_LoadSystemList.front(); g_LoadSystemList.pop();

		lua_getglobal(L, "LoadSystems");
			lua_newtable(L);
				lua_pushnumber(L, 1);
				lua_pushstring(L, sys.FileName.c_str());
			lua_settable(L, -3);

				lua_pushnumber(L, 2);
				lua_pushstring(L, sys.BaseName.c_str());
			lua_settable(L, -3);

				lua_pushnumber(L, i++);
				lua_pushvalue(L, -2);
			lua_settable(L, -4); // [i] = {sys.FileName,sys.BaseName}
		lua_pop(L, 2);
	}

	//Initialize systems
	lua_getglobal(L, "Initialize");
	if (lua_pcall(L, 0, 0, 0)) {
		lua_pushcfunction(L, shared_print);
		lua_pushvalue(L, -2);
		lua_call(L, 1, 0);
		lua_pop(L, 1);
	}

	//New userdata
	thread_userdata* userdata = new thread_userdata();
	userdata->finished = 0;
	userdata->L = L;
	userdata->current_time = Plat_FloatTime();
	LUA->PushUserType(userdata, 1);
	LUA->SetField(1, "_sim_userdata");
	lua_pushlightuserdata(L, userdata);
	lua_setglobal(L, "_userdata");

	//Create thread for simulation
	std::thread thread(threadSimulation, userdata);
	thread.detach();

	return 0;
}

LUA_FUNCTION( API_DeinitializeTrain ) 
{
	LUA->GetField(1,"_sim_userdata");
		thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
		if (userdata) userdata->finished = 1;
	LUA->Pop();

	LUA->PushNil();
	LUA->SetField(1,"_sim_userdata");

	return 0;
}

LUA_FUNCTION( API_LoadSystem ) 
{
	const char* basename = LUA->GetString(1);
	const char* name = LUA->GetString(2);
	if (!basename || !name)
		return 0;

	g_LoadSystemList.emplace(name, basename);
	return 0;
}

LUA_FUNCTION( API_RegisterSystem ) 
{
	const char* name = LUA->GetString(1);
	const char* filename = LUA->GetString(2);
	if (!name || !filename)
		return 0;
	
	g_MetrostroiSystemList.emplace_back(name, filename);
	ConMsg("Turbostroi: Registering system %s [%s]\n", name, filename);
	return 0;
}

LUA_FUNCTION( API_SendMessage ) 
{
	bool successful = false;
	LUA->CheckType(2,Type::Number);
	LUA->CheckType(3,Type::String);
	LUA->CheckType(4,Type::String);
	LUA->CheckType(5,Type::Number);
	LUA->CheckType(6,Type::Number);

	LUA->GetField(1,"_sim_userdata");
	thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
	LUA->Pop();

	if (userdata) {
		thread_msg tmsg;
		tmsg.message = (int)LUA->GetNumber(2);
		strncpy(tmsg.system_name,LUA->GetString(3),63);
		tmsg.system_name[63] = 0;
		strncpy(tmsg.name,LUA->GetString(4),63);
		tmsg.name[63] = 0;
		tmsg.index = LUA->GetNumber(5);
		tmsg.value = LUA->GetNumber(6);

		userdata->sim_to_thread_mutex.lock();
		userdata->sim_to_thread.push(tmsg);
		userdata->sim_to_thread_mutex.unlock();
		successful = true;
	}
	LUA->PushBool(successful);
	return 1;
}

LUA_FUNCTION( API_RecvMessages ) 
{
	LUA->GetField(1,"_sim_userdata");
	thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
	LUA->Pop();

	if (userdata) {
		LUA->CreateTable();
		userdata->thread_to_sim_mutex.lock();
		for (size_t i = 0; i < userdata->thread_to_sim.size(); ++i) {
			thread_msg tmsg = userdata->thread_to_sim.front();
			LUA->PushNumber(i);
			LUA->CreateTable();
			LUA->PushNumber(1);		LUA->PushNumber(tmsg.message);			LUA->RawSet(-3);
			LUA->PushNumber(2);		LUA->PushString(tmsg.system_name);		LUA->RawSet(-3);
			LUA->PushNumber(3);		LUA->PushString(tmsg.name);				LUA->RawSet(-3);
			LUA->PushNumber(4);		LUA->PushNumber(tmsg.index);			LUA->RawSet(-3);
			LUA->PushNumber(5);		LUA->PushNumber(tmsg.value);			LUA->RawSet(-3);
			LUA->RawSet(-3);
			userdata->thread_to_sim.pop();
		}
		userdata->thread_to_sim_mutex.unlock();
		return 1;
	}
	return 0;
}

LUA_FUNCTION( API_RecvMessage ) 
{
	LUA->GetField(1, "_sim_userdata");
	thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
	LUA->Pop();

	if (userdata) {
		userdata->thread_to_sim_mutex.lock();
		if (!userdata->thread_to_sim.empty())
		{
			thread_msg tmsg = userdata->thread_to_sim.front();
			LUA->PushNumber(tmsg.message);
			LUA->PushString(tmsg.system_name);
			LUA->PushString(tmsg.name);
			LUA->PushNumber(tmsg.index);
			LUA->PushNumber(tmsg.value);
			userdata->thread_to_sim.pop();
			userdata->thread_to_sim_mutex.unlock();
			return 5;
		}
		userdata->thread_to_sim_mutex.unlock();
	}
	return 0;
}

LUA_FUNCTION( API_ReadAvailable ) 
{
	LUA->GetField(1, "_sim_userdata");
	thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
	LUA->Pop();

	userdata->thread_to_sim_mutex.lock();
	LUA->PushNumber(userdata->thread_to_sim.size());
	userdata->thread_to_sim_mutex.unlock();
	return 1;
}

LUA_FUNCTION( API_SetSimulationFPS ) 
{
	LUA->CheckType(1,Type::Number);
	double FPS = LUA->GetNumber(1);
	if (!FPS)
		return 0;

	g_ThreadTickrate = 1000 / FPS;
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to %d FPS (%d ms delay)\n", (int)(FPS + 0.5), g_ThreadTickrate);
	return 0;
}

LUA_FUNCTION( API_SetMTAffinityMask ) 
{
	LUA->CheckType(1, Type::Number);
	int MTAffinityMask = (int)LUA->GetNumber(1);
	if (!MTAffinityMask)
		return 0;

#if defined(_WIN32)
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Main Thread Running on CPU%i\n", GetCurrentProcessorNumber());

	if (!SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(MTAffinityMask)))
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetMTAffinityMask failed!\n");
	else
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to CPU%i\n", GetCurrentProcessorNumber());
#elif defined(POSIX)
	cpu_set_t cpuSet;
	CPU_ZERO(&cpuSet);

	for (int i = 0; i < 32; i++)
		if (MTAffinityMask & (1 << i))
			CPU_SET(i, &cpuSet);

	if (sched_setaffinity(getpid(), sizeof(cpuSet), &cpuSet))
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetMTAffinityMask failed on train thread! (0x%04X)\n", errno);
	else
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to CPU%i\n", sched_getcpu());
#endif

	return 0;
}

LUA_FUNCTION( API_SetSTAffinityMask ) 
{
	LUA->CheckType(1, Type::Number);
	g_SimThreadAffinityMask = LUA->GetNumber(1);
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Assign Train Threads affinity to %d\n", g_SimThreadAffinityMask);
	return 0;
}

LUA_FUNCTION( API_StartRailNetwork ) 
{
	return 0;
}

//------------------------------------------------------------------------------
// Initialization SourceSDK
//------------------------------------------------------------------------------
LUA_FUNCTION(Think_handler) {
	g_TargetTime = Plat_FloatTime();
	
	g_SharedMessagesMutex.lock();
	if (!g_SharedMessagesQueue.empty())
	{
		shared_message msg = g_SharedMessagesQueue.front();
		ConColorMsg(Color(255, 0, 255, 255), msg.message);
		g_SharedMessagesQueue.pop();
	}
	g_SharedMessagesMutex.unlock();

	return 0;
}

void InstallHooks(ILuaBase* LUA) {
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Installing hooks!\n");
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Add");
				LUA->PushString("Think");
				LUA->PushString("Turbostroi_TargetTimeSync");
				LUA->PushCFunction(Think_handler);
			LUA->Call(3, 0);
	LUA->Pop(2);
}

void ClearLoadCache(const CCommand &command) {
	auto cacheSize = g_LoadedFilesCache.size();
	if (g_LoadedFilesCache.empty())
		ConColorMsg(Color(255, 255, 0, 255), "Turbostroi: No files in cache. Nothing to clear.\n");
	else
	{
		g_LoadedFilesCache.clear();
		ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Cleared %d files in cache!\n", cacheSize);
	}	
}

void ClearPrintQueue(const CCommand& command) {
	g_SharedMessagesMutex.lock();
	while(!g_SharedMessagesQueue.empty()) g_SharedMessagesQueue.pop();
	g_SharedMessagesMutex.unlock();
}

//------------------------------------------------------------------------------
// SourceSDK
//------------------------------------------------------------------------------
static SourceSDK::FactoryLoader ICvar_Loader("vstdlib");
static ICvar* p_ICvar = nullptr;

void InitInterfaces() {
	p_ICvar = ICvar_Loader.GetInterface<ICvar>(CVAR_INTERFACE_VERSION);
	if (p_ICvar == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load CVAR Interface!\n");
		return;
	}

	ConCommand* pClearCacheCommand = new ConCommand("turbostroi_clear_cache", ClearLoadCache, "Clear cache for reload systems", FCVAR_NOTIFY);
	ConCommand* pClearPrintCommand = new ConCommand("turbostroi_clear_print", ClearPrintQueue, "Clear print queue", FCVAR_NOTIFY);
	p_ICvar->RegisterConCommand(pClearCacheCommand);
	p_ICvar->RegisterConCommand(pClearPrintCommand);
}

//------------------------------------------------------------------------------
// Initialization
//------------------------------------------------------------------------------
GMOD_MODULE_OPEN() {
	InitInterfaces();
	//Check whether being ran on server
	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1,"SERVER");
		if (LUA->IsType(-1,Type::Nil)) {
			ConMsg("Turbostroi: DLL failed to initialize (gm_turbostroi.dll can only be used on server)\n");
			return 0;
		}
		LUA->Pop(); //SERVER

		//Check for global table
		LUA->GetField(-1,"Metrostroi");
		if (LUA->IsType(-1,Type::Nil)) {
			ConMsg("Turbostroi: DLL failed to initialize (cannot be used standalone without metrostroi addon)\n");
			return 0;
		}
		LUA->Pop(); //Metrostroi
		
		InstallHooks(LUA);

		//Initialize API
		LUA->CreateTable();
			PushCFunc(API_InitializeTrain, "InitializeTrain");
			PushCFunc(API_DeinitializeTrain, "DeinitializeTrain");
			PushCFunc(API_StartRailNetwork, "StartRailNetwork");

			PushCFunc(API_SendMessage, "SendMessage");
			PushCFunc(API_RecvMessages, "RecvMessages");
			PushCFunc(API_RecvMessage, "RecvMessage");

			PushCFunc(API_LoadSystem, "LoadSystem");
			PushCFunc(API_RegisterSystem, "RegisterSystem");

			PushCFunc(API_ReadAvailable, "ReadAvailable");
			PushCFunc(API_SetSimulationFPS, "SetSimulationFPS");
			PushCFunc(API_SetMTAffinityMask, "SetMTAffinityMask");
			PushCFunc(API_SetSTAffinityMask, "SetSTAffinityMask");
		LUA->SetField(-2,"Turbostroi");
	LUA->Pop();

	//Print some information
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL initialized (built " __DATE__ ")\n");
	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: Running with %i cores\n", std::thread::hardware_concurrency());

	// TODO for lock-free
	//if (!printMessages.is_lock_free()) {
	//	ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Not fully supported! \n");
	//}

	return 0;
}

//------------------------------------------------------------------------------
// Deinitialization
//------------------------------------------------------------------------------
GMOD_MODULE_CLOSE() {
	g_ForceThreadsFinished = true;
	if (p_ICvar != nullptr)
	{
		ConCommand* cmd = p_ICvar->FindCommand("turbostroi_clear_cache");
		if (cmd != nullptr) {
			p_ICvar->UnregisterConCommand(cmd);
		}

		cmd = p_ICvar->FindCommand("turbostroi_clear_print");
		if (cmd != nullptr)
		{
			cmd->Dispatch(CCommand());
			p_ICvar->UnregisterConCommand(cmd);
		}
	}

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "Metrostroi");
			if (LUA->IsType(-1, Type::Table)) {
				LUA->CreateTable();
				LUA->SetField(-2, "TurbostroiRegistered");
			}
		LUA->Pop();
		LUA->GetField(-1, "hook");
			LUA->GetField(-1, "Remove");
				LUA->PushString("Think");
				LUA->PushString("Turbostroi_TargetTimeSync");
				LUA->Call(2, 0);
	LUA->Pop(2);
	
	LUA->PushNil();
	LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "Turbostroi");

	ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: DLL unloaded.\n");
	return 0;
}