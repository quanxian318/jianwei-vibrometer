// MinGW-w64 lacks this Windows SDK header; WebView2.h only needs the type.
#pragma once
typedef struct EventRegistrationToken { __int64 value; } EventRegistrationToken;
