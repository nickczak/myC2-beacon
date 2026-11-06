#pragma once

#ifdef __cplusplus
#include <string>
extern "C"
{
#endif

  int POST(const char *server_address, const char *data, const char *ca_file);

#ifdef __cplusplus
}

inline int
POST(const std::string &server_address, const std::string &data, const std::string &ca_file)
{
  return POST(server_address.c_str(), data.c_str(), ca_file.empty() ? nullptr : ca_file.c_str());
}
#endif
