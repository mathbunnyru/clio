{% set compiler, sani = profile_name.split('.') %}
{% if sani == asan %}
    {% set sanitizer = address %}
{% elif sani == tsan %}
    {% set sanitizer = thread %}
{% elif sani == ubsan %}
    {% set sanitizer = undefined %}
{% endif %}

include({{ compiler }})

[options]
boost/*:extra_b2_flags="cxxflags=\"-fsanitize={{ sanitizer }}\" linkflags=\"-fsanitize={{ sanitizer }}\""
boost/*:without_stacktrace=True

[conf]
tools.build:cflags+=["-fsanitize={{ sanitizer }}"]
tools.build:cxxflags+=["-fsanitize={{ sanitizer }}"]
tools.build:ldflags+=["-fsanitize={{ sanitizer }}"]
