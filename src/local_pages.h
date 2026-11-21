#pragma once

#include "browser_state.h"

#include <string>

namespace glassglide {

enum class NavigationDecision { AllowLocal, OpenPage, Block };

NavigationDecision decide_navigation(const char* uri, bool is_navigation, Page& page);
std::string local_page(Page page);

} // namespace glassglide
