// 完整实现验证测试 - 展示5项改进
// 编译: polyc complete_implementation_test.cpp -o test

// ============ 改进1: 虚函数检测（使用 virtual 关键字）============
class Shape {
public:
    int id;
    
    // ✅ 使用标准 C++ 语法 virtual 关键字（不再需要 [[virtual]] attribute）
    virtual int area() {
        return 0;
    }
    
    virtual int perimeter() {
        return 0;
    }
    
    // ✅ 虚析构函数
    virtual ~Shape() {}
};

// ============ 改进2: 完整类型系统 ============
class Point {
public:
    // ✅ 不同类型的字段（不再假设所有字段都是 i64）
    double x;      // f64 类型
    double y;      // f64 类型
    int* ref;      // i32* 指针类型
    bool valid;    // i1 布尔类型
    
    Point(double px, double py) {
        x = px;
        y = py;
        valid = true;
    }
    
    double distance() {
        // ✅ 浮点运算
        return x * x + y * y;  // 简化的距离平方
    }
};

// ============ 改进3: new/delete 和构造/析构函数 ============
class Rectangle : public Shape {
public:
    double width;   // ✅ f64 类型
    double height;  // ✅ f64 类型
    
    // ✅ 构造函数（自动初始化 vtable 指针）
    Rectangle(double w, double h) {
        width = w;
        height = h;
    }
    
    // ✅ 重写虚函数
    int area() override {
        return width * height;  // 简化为整数返回
    }
    
    int perimeter() override {
        return 2 * (width + height);
    }
    
    // ✅ 析构函数
    ~Rectangle() {
        // 清理资源
    }
};

// ============ 改进4: 多继承支持 ============
class Printable {
public:
    virtual void print() {}
};

class Serializable {
public:
    virtual void serialize() {}
};

// ✅ 多继承（每个基类有独立的 vtable 指针）
class Document : public Printable, public Serializable {
private:
    int page_count;  // ✅ private 字段
    
public:
    void print() override {
        // 打印文档
    }
    
    void serialize() override {
        // 序列化文档
    }
    
    int get_pages() {
        return page_count;  // ✅ 本类方法可访问 private
    }
};

// ============ 改进5: 访问控制检查 ============
class BankAccount {
private:
    double balance;      // ✅ private: 仅本类可访问
    
protected:
    int account_id;      // ✅ protected: 本类和派生类可访问
    
public:
    void deposit(double amount) {
        balance += amount;  // ✅ 正确：本类可访问 private
    }
    
    double get_balance() {
        return balance;  // ✅ 正确：通过 public 方法访问
    }
};

class SavingsAccount : public BankAccount {
public:
    void set_id(int id) {
        account_id = id;  // ✅ 正确：派生类可访问 protected
        // balance = 0;   // ❌ 错误：派生类不能访问基类的 private
    }
};

// ============ 综合测试 ============
int main() {
    // ✅ new 表达式（调用构造函数，初始化 vtable）
    Shape* shape = new Rectangle(10, 20);
    
    // ✅ 虚函数调用（通过 vtable）
    int area = shape->area();
    
    // ✅ 多态
    int perim = shape->perimeter();
    
    // ✅ delete 表达式（调用析构函数，释放内存）
    delete shape;
    
    // ✅ 多继承
    Document* doc = new Document();
    doc->print();
    doc->serialize();
    delete doc;
    
    // ✅ 访问控制
    BankAccount* account = new BankAccount();
    account->deposit(100);  // ✅ 通过 public 方法
    // account->balance += 50;  // ❌ 编译错误：无法访问 private
    delete account;
    
    return 0;
}
