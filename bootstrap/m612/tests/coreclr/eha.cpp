// Self-test of src/coreclr/minipal/Windows/mingw_eha.cpp (/EHa semantics for clang or GCC/MinGW):
// destructors and catch(...) for SEH exceptions, __except over C++ frames, rethrow.
// Built and run by coreclr.sh test.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#if defined(__GNUC__) && !defined(__clang__)
#include <pseh/pseh2_64.h>     // __try/__except of ReactOS's gcc_plugin_seh
#else
#define __endtry
#endif
static char log_[4096]; static void L(const char* s) { strcat(log_, s); strcat(log_, " "); }
struct D { const char* n; D(const char* n):n(n){} ~D(){ L(n); } };
__attribute__((noinline)) void fault() { volatile int* p = (int*)(ULONG_PTR)rand() == (int*)1 ? (int*)8 : nullptr; *p = 1; }
__attribute__((noinline)) void raise_(DWORD c) { ULONG_PTR a[2] = {1, 2}; RaiseException(c, 0, 2, a); }
__attribute__((noinline)) void lvl2(int k) { D d("d2"); if (k == 0) fault(); else if (k == 1) raise_(0xE0434352); else if (k == 2) throw 42; }
__attribute__((noinline)) void lvl1(int k) { D d("d1"); lvl2(k); }
#define CHECK(name, cond) do { printf("%-55s %s   [%s]\n", name, (cond) ? "ok" : "FAIL", log_); log_[0]=0; } while (0)
int seh_code(int k) { __try { lvl1(k); } __except (EXCEPTION_EXECUTE_HANDLER) { return (int)GetExceptionCode(); } __endtry return 0; }
int catchall(int k) { try { D d("d0"); lvl1(k); } catch (...) { L("catch"); return 1; } return 0; }
int typed_then_outer(int k) { try { try { lvl1(k); } catch (int) { L("int"); return 2; } } catch (...) { L("outer"); return 1; } return 0; }
__attribute__((noinline)) void rethrow_inner(int k) { try { lvl1(k); } catch (...) { L("c1"); throw; } }
int rethrow_(int k) { __try { rethrow_inner(k); } __except (EXCEPTION_EXECUTE_HANDLER) { L("x"); return (int)GetExceptionCode(); } __endtry return 0; }
int nested() { try { throw 7; } catch (int) { try { lvl1(0); } catch (...) { L("inner"); } L("outer"); return 1; } return 0; }
int main() {
  int r;
  r = seh_code(0); CHECK("AV -> __except, dtors of C++ frames run", r == (int)0xC0000005 && !strcmp(log_, "d2 d1 "));
  r = seh_code(1); CHECK("RaiseException -> __except, dtors run", r == (int)0xE0434352 && !strcmp(log_, "d2 d1 "));
  r = seh_code(2); CHECK("C++ throw -> __except, dtors run", r == 0x20474343 && !strcmp(log_, "d2 d1 "));
  r = catchall(0); CHECK("AV -> catch(...) (EHa)", r == 1 && !strcmp(log_, "d2 d1 d0 catch "));
  r = catchall(1); CHECK("RaiseException -> catch(...) (EHa)", r == 1 && !strcmp(log_, "d2 d1 d0 catch "));
  r = catchall(2); CHECK("C++ throw -> catch(...)", r == 1 && !strcmp(log_, "d2 d1 d0 catch "));
  r = typed_then_outer(0); CHECK("AV skips catch(int), outer catch(...)", r == 1 && !strcmp(log_, "d2 d1 outer "));
  r = typed_then_outer(2); CHECK("C++ int -> catch(int)", r == 2 && !strcmp(log_, "d2 d1 int "));
  r = rethrow_(0); CHECK("throw; of an AV re-raises it", r == (int)0xC0000005 && !strcmp(log_, "d2 d1 c1 x "));
  r = nested(); CHECK("AV caught inside a catch block", r == 1 && !strcmp(log_, "d2 d1 inner outer "));
  for (int i = 0; i < 100000; i++) { catchall(i % 3); seh_code(i % 3); log_[0] = 0; }
  CHECK("200000 more round trips", 1);
  return 0;
}
