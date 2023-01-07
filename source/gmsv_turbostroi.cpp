#include "gmsv_turbostroi.h"
using namespace GarrysMod::Lua;

//------------------------------------------------------------------------------
// SourceSDK
//------------------------------------------------------------------------------
static SourceSDK::FactoryLoader icvar_loader("vstdlib");
static ICvar* p_ICvar = nullptr;

//------------------------------------------------------------------------------
// Lua Utils
//------------------------------------------------------------------------------
static void stackDump(lua_State *L) {
	int i;
	int top = lua_gettop(L);
	for (i = 1; i <= top; i++) {  /* repeat for each level */
		int t = lua_type(L, i);
		switch (t) {

		case LUA_TSTRING:  /* strings */
			ConColorMsg(Color(255, 0, 0, 255), "`%s'", lua_tostring(L, i));
			break;

		case LUA_TBOOLEAN:  /* booleans */
			ConColorMsg(Color(255, 0, 0, 255), lua_toboolean(L, i) ? "true" : "false");
			break;

		case LUA_TNUMBER:  /* numbers */
			ConColorMsg(Color(255, 0, 0, 255), "%g", lua_tonumber(L, i));
			break;

		default:  /* other values */
			ConColorMsg(Color(255, 0, 0, 255), "%s", lua_typename(L, t));
			break;

		}
		ConColorMsg(Color(255, 0, 0, 255), "  ");  /* put a separator */
	}
	ConColorMsg(Color(255, 0, 0, 255), "\n");  /* end the listing */
}

//------------------------------------------------------------------------------
// Shared thread printing stuff
//------------------------------------------------------------------------------
double TargetTime = 0.0;
int ThreadTickrate = 10;
char MetrostroiSysmesList[BUFFER_SIZE] = { 0 };
char LoadSystemsList[BUFFER_SIZE] = { 0 };
int SimThreadAffinityMask = 0;
std::map<std::string, std::string> LoadedFilesCache;

//------------------------------------------------------------------------------
// Turbostroi sim thread API
//------------------------------------------------------------------------------
std::queue <shared_message> SharedMessagesQueue;
std::mutex SharedMessagesMutex;
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
	std::unique_lock<std::mutex> lock(SharedMessagesMutex);
	SharedMessagesQueue.push(msg);
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
		std::unique_lock<std::mutex> lock(userdata->thread_to_sim_mutex);
		userdata->thread_to_sim.push(tmsg);
		successful = true;
	}
	return successful;
}

int thread_recvmessages(lua_State* state) {
	lua_getglobal(state,"_userdata");
	thread_userdata* userdata = (thread_userdata*)lua_touserdata(state,-1);
	lua_pop(state,1);

	if (userdata) {
		std::unique_lock<std::mutex> lock(userdata->sim_to_thread_mutex);
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
		return 1;
	}
	return 0;
}

extern "C" TURBOSTROI_EXPORT thread_msg ThreadRecvMessage(void* p) {
	thread_userdata* userdata = (thread_userdata*)p;
	thread_msg tmsg;
	//tmsg.message = NULL;
	if (userdata) {
		std::unique_lock<std::mutex> lock(userdata->sim_to_thread_mutex);
		tmsg = userdata->sim_to_thread.front();
		userdata->sim_to_thread.pop();
	}
	return tmsg;
}

extern "C" TURBOSTROI_EXPORT int ThreadReadAvailable(void* p) {
	thread_userdata* userdata = (thread_userdata*)p;

	std::unique_lock<std::mutex> lock(userdata->sim_to_thread_mutex);
	return userdata->sim_to_thread.size();
}

//------------------------------------------------------------------------------
// Turbostroi v2 Logic
//------------------------------------------------------------------------------
void threadSimulation(thread_userdata* userdata) {

	if (SimThreadAffinityMask)
	{

#if defined(_WIN32)
		if (!SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(SimThreadAffinityMask))) {
			ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: SetSTAffinityMask failed on train thread!\n");
		}
#elif defined(POSIX)
		cpu_set_t cpuSet;
		CPU_ZERO(&cpuSet);

		for (int i = 0; i < 32; i++)
			if (SimThreadAffinityMask & (1 << i))
				CPU_SET(i, &cpuSet);

		if (sched_setaffinity(getpid(), sizeof(cpuSet), &cpuSet))
			ConColorMsg(Color(255, 0, 255, 255), "Turbostroi: SetSTAffinityMask failed on train thread! (0x%04X)\n",errno);
#endif
	}

	lua_State* L = userdata->L;

	while (userdata && !userdata->finished) {
		lua_settop(L,0);

		//Simulate one step
		if (userdata->current_time < TargetTime) {
			userdata->current_time = TargetTime;
			lua_pushnumber(L, Plat_FloatTime());
			lua_setglobal(L, "CurrentTime");

			//Execute think
			lua_getglobal(L,"Think");
			lua_pushboolean(L, false);
			if (lua_pcall(L, 1, 0, 0)) {
				std::string err = lua_tostring(L, -1);
				err += "\n";
				shared_message msg;
				strcpy(msg.message, err.c_str());
				std::unique_lock<std::mutex> lock(SharedMessagesMutex);
				SharedMessagesQueue.push(msg);
				lock.unlock();
			}
		}
		else {
			//Execute think
			lua_pushnumber(L, Plat_FloatTime());
			lua_setglobal(L, "CurrentTime");

			lua_getglobal(L, "Think");
			lua_pushboolean(L, true);
			if (lua_pcall(L, 1, 0, 0)) {
				std::string err = lua_tostring(L, -1);
				err += "\n";
				shared_message msg;
				strcpy(msg.message, err.c_str());
				std::unique_lock<std::mutex> lock(SharedMessagesMutex);
				SharedMessagesQueue.push(msg);
				lock.unlock();
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(ThreadTickrate));
	}

	//Release resources
	lua_pushcfunction(L,shared_print);
	lua_pushstring(L,"[!] Terminating train thread");
	lua_call(L,1,0);
	lua_close(L);
	free(userdata);
}

//------------------------------------------------------------------------------
// Metrostroi Lua API
//------------------------------------------------------------------------------
void load(GarrysMod::Lua::ILuaBase* LUA, lua_State* L, const char* filename, const char* path, const char* variable = NULL, const char* defpath = NULL, bool json = false) {
	//Load up "sv_turbostroi.lua" in the new JIT environment
	const char* file_data = NULL;
	auto cache_item = LoadedFilesCache.find(filename);
	if (cache_item != LoadedFilesCache.end()) {
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
			LoadedFilesCache.emplace(filename, file_data);
		}
		LUA->Pop(); //returned result
		LUA->Pop(); //file
		LUA->Pop(); //GLOB
	}

	if (file_data) {
		if (!variable) {
			if (luaL_loadbuffer(L, file_data, strlen(file_data), filename) || lua_pcall(L, 0, LUA_MULTRET, 0)) {
				ConColorMsg(Color(255, 0, 127, 255), lua_tostring(L, -1));
				lua_pop(L, 1);
			}
		} else {
			if (!json) {
				lua_pushstring(L, file_data);
				lua_setglobal(L, variable);
			}
			else {
				LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
				LUA->GetField(-1, "table");
				LUA->GetField(-1, "ToString");
				LUA->Remove(-2);
				LUA->GetField(-2, "table");
				LUA->GetField(-1, "Sanitise");
				LUA->Remove(-2);
				LUA->GetField(-3, "util");
				LUA->GetField(-1, "JSONToTable");
				LUA->Remove(-2);
				LUA->PushString(file_data);
				LUA->Call(1, 1);
				LUA->Call(1, 1);
				LUA->PushString(variable);
				LUA->PushBool(false);
				LUA->Call(3, 1);

				if (luaL_loadbuffer(L, LUA->GetString(-1), strlen(LUA->GetString(-1)), filename) || lua_pcall(L, 0, LUA_MULTRET, 0)) {
					ConColorMsg(Color(255, 0, 127, 255), lua_tostring(L, -1));
					lua_pop(L, 1);
				}

				LUA->Pop(); //returned result
				LUA->Pop(); //GLOB
			}
		}
	} else {
		if (defpath) {
			ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s' in '%s' path) Trying to load default file in '%s' path\n", filename, path, defpath);
			load(LUA, L, filename, defpath, variable, NULL, json);
		}
		else {
			ConColorMsg(Color(255, 0, 127, 255), "Turbostroi: File not found! ('%s' in '%s' path)\n", filename, path);
		}
	}
}

LUA_FUNCTION( API_InitializeTrain ) 
{
	//Initialize LuaJIT for train
	lua_State* L = luaL_newstate();
	luaL_openlibs(L);
	luaopen_bit(L);
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
	char* systems = MetrostroiSysmesList;
	while (systems[0]) {
		char filename[8192] = { 0 };
		char* system_name = systems;
		char* system_path = strchr(systems, '\n') + 1;

		strncpy(filename, system_path, strchr(system_path, '\n') - system_path);
		load(LUA, L, filename, "LUA");
		systems = system_path + strlen(filename) + 1;
	}

	//Initialize all the systems reported by the train
	systems = LoadSystemsList;
	while (systems[0]) {
		char* system_name = systems;
		char* system_type = strchr(systems, '\n') + 1;
		*(strchr(system_name, '\n')) = 0;
		*(strchr(system_type, '\n')) = 0;

		lua_getglobal(L, "LoadSystems");
		lua_pushstring(L, system_type);
		lua_setfield(L, -2, system_name);

		systems = system_type + strlen(system_type) + 1;
	}
	LoadSystemsList[0] = 0;

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

	strncat(LoadSystemsList, basename,131071);
	strncat(LoadSystemsList,"\n",131071);
	strncat(LoadSystemsList, name,131071);
	strncat(LoadSystemsList,"\n",131071);
	return 0;
}

LUA_FUNCTION( API_RegisterSystem ) 
{
	const char* name = LUA->GetString(1);
	const char* filename = LUA->GetString(2);
	if (!name || !filename)
		return 0;
	
	strncat(MetrostroiSysmesList, name,131071);
	strncat(MetrostroiSysmesList,"\n",131071);
	strncat(MetrostroiSysmesList, filename,131071);
	strncat(MetrostroiSysmesList,"\n",131071);
	ConMsg("Metrostroi: Registering system %s [%s]\n", name, filename);
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
		std::unique_lock<std::mutex> lock(userdata->sim_to_thread_mutex);
		userdata->sim_to_thread.push(tmsg);
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
		std::unique_lock<std::mutex> lock(userdata->thread_to_sim_mutex);
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
		std::unique_lock<std::mutex> lock(userdata->thread_to_sim_mutex);
		if (!userdata->thread_to_sim.empty())
		{
			thread_msg tmsg = userdata->thread_to_sim.front();
			LUA->PushNumber(tmsg.message);
			LUA->PushString(tmsg.system_name);
			LUA->PushString(tmsg.name);
			LUA->PushNumber(tmsg.index);
			LUA->PushNumber(tmsg.value);
			userdata->thread_to_sim.pop();
			return 5;
		}
	}
	return 0;
}

LUA_FUNCTION( API_ReadAvailable ) 
{
	LUA->GetField(1, "_sim_userdata");
	thread_userdata* userdata = LUA->GetUserType<thread_userdata>(-1, 1);
	LUA->Pop();

	std::unique_lock<std::mutex> lock(userdata->thread_to_sim_mutex);
	LUA->PushNumber(userdata->thread_to_sim.size());
	return 1;
}

LUA_FUNCTION( API_SetSimulationFPS ) 
{
	LUA->CheckType(1,Type::Number);
	double FPS = LUA->GetNumber(1);
	if (!FPS)
		return 0;

	ThreadTickrate = 1000 / FPS;
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Changed to %d FPS (%d ms delay)\n", (int)(FPS + 0.5), ThreadTickrate);
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
	SimThreadAffinityMask = LUA->GetNumber(1);
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Assign Train Threads affinity to %d\n", SimThreadAffinityMask);
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
	TargetTime = Plat_FloatTime();
	
	std::unique_lock<std::mutex> lock(SharedMessagesMutex);
	if (!SharedMessagesQueue.empty())
	{
		shared_message msg = SharedMessagesQueue.front();
		ConColorMsg(Color(255, 0, 255, 255), msg.message);
		SharedMessagesQueue.pop();
	}

	return 0;
}

void InstallHooks(ILuaBase* LUA) {
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Installing hooks!\n");
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
	if (!strcmp(command.Arg(1), "1"))
	{
		ConMsg("Files in cache:\n");
		if (LoadedFilesCache.empty())
			ConMsg("\tNo files in cache.\n");
		else
		{
			for (const auto& [key, value] : LoadedFilesCache) {
				ConMsg("\t%s\n", key.c_str());
			}
		}
	}
	LoadedFilesCache.clear();
	ConColorMsg(Color(0, 255, 0, 255), "Turbostroi: Cache cleared!\n");
}

void InitInterfaces() {
	p_ICvar = icvar_loader.GetInterface<ICvar>(CVAR_INTERFACE_VERSION);
	if (p_ICvar == nullptr)
	{
		ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Unable to load CVAR Interface!\n");
		return;
	}

	ConCommand* pCommand = new ConCommand("turbostroi_clear_cache", ClearLoadCache, "Clear cache for reload systems", FCVAR_NOTIFY);
	p_ICvar->RegisterConCommand(pCommand);
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
			ConMsg("Metrostroi: DLL failed to initialize (gm_turbostroi.dll can only be used on server)\n");
			return 0;
		}
		LUA->Pop(); //SERVER

		//Check for global table
		LUA->GetField(-1,"Metrostroi");
		if (LUA->IsType(-1,Type::Nil)) {
			ConMsg("Metrostroi: DLL failed to initialize (cannot be used standalone without metrostroi addon)\n");
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
	ConMsg("Metrostroi: DLL initialized (built " __DATE__ ")\n");
	ConMsg("Metrostroi: Running with %i cores\n", std::thread::hardware_concurrency());

	// TODO
	//if (!printMessages.is_lock_free()) {
	//	ConColorMsg(Color(255, 0, 0, 255), "Turbostroi: Not fully supported! \n");
	//}

	return 0;
}

//------------------------------------------------------------------------------
// Deinitialization
//------------------------------------------------------------------------------
GMOD_MODULE_CLOSE() {
	if (p_ICvar != nullptr)
	{
		ConCommand* cmd = p_ICvar->FindCommand("turbostroi_clear_cache");
		if (cmd != nullptr) {
			p_ICvar->UnregisterConCommand(cmd);
		}
	}

	LUA->PushSpecial(GarrysMod::Lua::SPECIAL_GLOB);
		LUA->GetField(-1, "Metrostroi");
		if (LUA->IsType(-1, Type::Table)) {
			LUA->CreateTable();
			LUA->SetField(-2, "TurbostroiRegistered");
		}
	LUA->Pop(2);
	
	LUA->PushNil();
	LUA->SetField(GarrysMod::Lua::INDEX_GLOBAL, "Turbostroi");
	return 0;
}