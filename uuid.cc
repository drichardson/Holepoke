#include "uuid.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#elif defined __linux__
#include <uuid/uuid.h>
#elif defined WIN32
#include <Rpc.h>
#endif

bool holepoke::GetUUID(std::string & result)
{
#ifdef __APPLE__
	CFUUIDRef uuid = CFUUIDCreate(NULL);
	CFStringRef uuidString = CFUUIDCreateString(NULL, uuid);
	
	// Call CFStringGetCStringPtr with kCFStringEncodingMacRoman because that is the only one that works.
	// Otherwise, we have to fall back onto CFStringGetCString, which we still do just in case this stops working.
	char buf[100];
	const char* str = CFStringGetCStringPtr(uuidString, kCFStringEncodingMacRoman);
	
	if ( str == NULL )
	{
		// puts("Falling back to get C string");
		if ( CFStringGetCString(uuidString, buf, sizeof(buf), kCFStringEncodingMacRoman) )
		{
			str = buf;
		}
	}
	
	if ( str )
	{
		result = str;
	}
	
	CFRelease(uuidString);
	CFRelease(uuid);
	
	return str != NULL;
#elif __linux__
	uuid_t uuid;
	char uuidString[36 + 1];
	uuid_generate_random(uuid);
	uuid_unparse_upper(uuid, uuidString);
	result = uuidString;
	return true;
#else
	UUID uuid;
	RPC_STATUS rc = UuidCreate(&uuid);

	if ( rc == RPC_S_OK )
	{
		RPC_CSTR stringUuid = NULL;
		UuidToString(&uuid, &stringUuid);
//#error Zac: would you get this one too? I'm done for the night.
// TODO: Need to get a std::string from stringUuid, but I think stringUuid is a wide character pointer (i.e. each character is 2 bytes)
		std::string c_string((char*)stringUuid);
		RpcStringFree(&stringUuid);
		result = c_string;
		return true;
	}

	return false;
#endif
}
