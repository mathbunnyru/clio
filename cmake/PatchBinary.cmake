#[===================================================================[
   Patch executables to run in non-Nix environments.

   The Nix-based CI image links binaries against an ELF interpreter (loader)
   that lives in the Nix store, so the resulting binaries don't run elsewhere
   (including once installed from the .deb package). `clio_patch_binary` adds a
   POST_BUILD step that resets the interpreter to the system default loader and
   drops the rpath.

   This is only active inside the Nix-based image, detected by the presence of
   /tmp/loader-path.sh (shipped by that image, resolves the default loader). It
   is skipped for sanitizer builds, whose runtime libraries are resolved through
   the rpath. Everywhere else `clio_patch_binary` is a no-op.
#]===================================================================]

include_guard(GLOBAL)

# Provided by the Nix-based CI image; prints the system default ELF loader path.
set(_clio_loader_path_script "/tmp/loader-path.sh")

if(
    CMAKE_SYSTEM_NAME STREQUAL "Linux"
    AND NOT SANITIZERS_ENABLED
    AND EXISTS "${_clio_loader_path_script}"
)
    execute_process(
        COMMAND "${_clio_loader_path_script}"
        OUTPUT_VARIABLE CLIO_DEFAULT_LOADER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    find_program(PATCHELF_COMMAND patchelf REQUIRED)
    set(CLIO_PATCH_BINARIES TRUE)
    message(
        STATUS
        "Binaries will be patched to use loader '${CLIO_DEFAULT_LOADER}'"
    )
else()
    set(CLIO_PATCH_BINARIES FALSE)
endif()

function(clio_patch_binary target)
    if(NOT CLIO_PATCH_BINARIES)
        return()
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            "${PATCHELF_COMMAND}" --set-interpreter "${CLIO_DEFAULT_LOADER}"
            --remove-rpath "$<TARGET_FILE:${target}>"
        COMMENT "Patching ${target}: set default loader, remove rpath"
        VERBATIM
    )
endfunction()
