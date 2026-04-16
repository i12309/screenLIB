#include "WebSocketLink.h"

// Определение статического члена — ровно здесь, в одном .cpp файле.
// Если получите ошибку линковки "undefined reference to s_instance",
// убедитесь что этот файл включён в сборку.
WebSocketLink* WebSocketLink::s_instance = nullptr;
