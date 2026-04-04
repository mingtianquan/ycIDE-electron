export function generateCommonDebugRuntimeCode(): string {
  let result = ''

  // Windows-specific console encoding setup
  result += '#ifdef _WIN32\n'
  result += '#include <fcntl.h>\n'
  result += '#include <io.h>\n'
  result += 'static bool g_yc_dbg_console_encoding_set = false;\n'
  result += 'static void yc_dbg_ensure_console_utf8() {\n'
  result += '    if (!g_yc_dbg_console_encoding_set) {\n'
  result += '        _setmode(_fileno(stdout), _O_U8TEXT);\n'
  result += '        g_yc_dbg_console_encoding_set = true;\n'
  result += '    }\n'
  result += '}\n'
  result += '#else\n' // For non-Windows platforms, define a dummy function
  result += 'static void yc_dbg_ensure_console_utf8() {}\n'
  result += '#endif\n\n'

  result += 'static long long g_yc_dbg_resume_token = 0;\n'

  // Call the console setup function in yc_dbg_break_begin
  result += 'static void yc_dbg_break_begin(const char* fileName, int lineNo) {\n'
  result += '    yc_dbg_ensure_console_utf8();\n' // Call the function here
  result += '    printf("__YCDBG_BREAK_BEGIN__|%s|%d\\n", fileName ? fileName : "", lineNo);\n'
  result += '    fflush(stdout);\n'
  result += '}\n\n'

  // Call the console setup function in yc_dbg_break_end
  result += 'static void yc_dbg_break_end(void) {\n'
  result += '    yc_dbg_ensure_console_utf8();\n' // Call the function here
  result += '    printf("__YCDBG_BREAK_END__\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n\n'

  // Call the console setup function in yc_dbg_emit_var_prefix
  result += 'static void yc_dbg_emit_var_prefix(const char* name, const char* type) {\n'
  result += '    yc_dbg_ensure_console_utf8();\n' // Call the function here
  result += '    printf("__YCDBG_VAR__|%s|%s|", name ? name : "", type ? type : "");\n'
  result += '}\n\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, const wchar_t* value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value ? value : L"");\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, wchar_t* value) {\n'
  result += '    yc_dbg_emit_var(name, type, (const wchar_t*)value);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, const char* value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value ? value : "");\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, char* value) {\n'
  result += '    yc_dbg_emit_var(name, type, (const char*)value);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, const YC_BIN& value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value);\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, float value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value);\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n'
  result += 'static void yc_dbg_emit_var(const char* name, const char* type, double value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value);\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n'
  result += 'template <typename T> static void yc_dbg_emit_var(const char* name, const char* type, T value) {\n'
  result += '    yc_dbg_emit_var_prefix(name, type);\n'
  result += '    yc_debug_line_part(value);\n'
  result += '    printf("\\n");\n'
  result += '    fflush(stdout);\n'
  result += '}\n\n'
  return result
}
