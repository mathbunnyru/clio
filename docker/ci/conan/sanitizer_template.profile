{% set compiler, sani = profile_name.split('.') %}

{% set sanitizer_opt_map = {'asan': 'address', 'tsan': 'thread', 'ubsan': 'undefined'} %}
{% set sanitizer = sanitizer_opt_map[sani] %}
{% set sanitizer_build_flags = "-fsanitize=" ~ sanitizer ~ " -g -O1 -fno-omit-frame-pointer" %}
{% set sanitizer_link_flags = "-fsanitize=" ~ sanitizer %}

include({{ compiler }})

[options]
boost/*:extra_b2_flags = "cxxflags=\"{{ sanitizer_build_flags }}\" linkflags=\"{{ sanitizer_link_flags }}\""
boost/*:without_stacktrace = True

[conf]
tools.build:cflags += ["{{ sanitizer_build_flags }}"]
tools.build:cxxflags += ["{{ sanitizer_build_flags }}"]
tools.build:exelinkflags += ["{{ sanitizer_link_flags }}"]
tools.build:sharedlinkflags += ["{{ sanitizer_link_flags }}"]
