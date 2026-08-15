#ifndef ZOAL_ATC_NAVDATA_MD5_HPP
#define ZOAL_ATC_NAVDATA_MD5_HPP

#include <string>
#include <string_view>

namespace zoal_atc::navdata {

// md5_hex returns the lowercase hex MD5 digest of data. It is the navdata
// cache key shared with the console (console/internal/navdata.Hash): matching
// hashes mean the console's persisted parse is still valid and the full text
// need not be re-sent. MD5 is used as a change-detection fingerprint, not for
// security.
std::string md5_hex(std::string_view data);

} // namespace zoal_atc::navdata

#endif // ZOAL_ATC_NAVDATA_MD5_HPP
