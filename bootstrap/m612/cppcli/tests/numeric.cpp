using namespace System;
int main() {
    long long value = 5000000002LL;
    Console::WriteLine(value++);
    Console::WriteLine(++value);
    double floating = 2.5;
    Console::WriteLine(++floating == 3.5);
    double nan = 0.0 / 0.0;
    Console::WriteLine(nan < 1.0);
    Console::WriteLine(nan > 1.0);
    Console::WriteLine(nan <= 1.0);
    Console::WriteLine(nan >= 1.0);
    unsigned int big = 4000000000U;
    Console::WriteLine(big > 1U);
    return 0;
}
