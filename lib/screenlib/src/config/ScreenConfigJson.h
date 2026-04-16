#pragma once

#include <stddef.h>

#include "ScreenConfig.h"

namespace screenlib {

// Утилита парсинга JSON -> ScreenConfig.
class ScreenConfigJson {
public:
    // Парсит JSON-конфиг и заполняет out.
    // Возвращает true при успехе.
    // При ошибке пишет короткий текст в errBuf (если задан).
    static bool parse(const char* json,
                      ScreenConfig& out,
                      char* errBuf = nullptr,
                      size_t errBufSize = 0);
};

}  // namespace screenlib

