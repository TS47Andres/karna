#pragma once

#include <curl/curl.h>

// Configure certificate verification for packaged builds. The bundled CA
// file is resolved relative to the executable, so the working directory does
// not affect HTTPS requests.
void configure_curl_ssl(CURL* curl);
