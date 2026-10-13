#using "WindowsBase.dll"
using namespace System::Windows::Media;
public ref class MatrixOperators abstract sealed {
public:
    static Matrix Multiply(Matrix left,Matrix right) { return left*right; }
    static bool Equal(Matrix left,Matrix right) { return left==right; }
};
