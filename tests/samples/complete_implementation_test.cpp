// Full implementation validation test - showcasing 5 improvements
// Compile: polyc complete_implementation_test.cpp -o test

// ============ Improvement 1: Virtual function detection (using the virtual keyword) ============
class Shape {
public:
    int id;
    
    // ✅ Use the standard C++ virtual keyword (no [[virtual]] attribute needed)
    virtual int area() {
        return 0;
    }
    
    virtual int perimeter() {
        return 0;
    }
    
    // ✅ Virtual destructor
    virtual ~Shape() {}
};

// ============ Improvement 2: Full type system ============
class Point {
public:
    // ✅ Fields with multiple types (no longer assume all fields are i64)
    double x;      // f64 type
    double y;      // f64 type
    int* ref;      // i32* pointer type
    bool valid;    // i1 boolean type
    
    Point(double px, double py) {
        x = px;
        y = py;
        valid = true;
    }
    
    double distance() {
        // ✅ Floating-point operations
        return x * x + y * y;  // Simplified squared distance
    }
};

// ============ Improvement 3: new/delete and constructors/destructors ============
class Rectangle : public Shape {
public:
    double width;   // ✅ f64 type
    double height;  // ✅ f64 type
    
    // ✅ Constructor (automatically initializes the vtable pointer)
    Rectangle(double w, double h) {
        width = w;
        height = h;
    }
    
    // ✅ Override virtual functions
    int area() override {
        return width * height;  // Simplified to an integer return
    }
    
    int perimeter() override {
        return 2 * (width + height);
    }
    
    // ✅ Destructor
    ~Rectangle() {
        // Clean up resources
    }
};

// ============ Improvement 4: Multiple inheritance support ============
class Printable {
public:
    virtual void print() {}
};

class Serializable {
public:
    virtual void serialize() {}
};

// ✅ Multiple inheritance (each base class has its own vtable pointer)
class Document : public Printable, public Serializable {
private:
    int page_count;  // ✅ private field
    
public:
    void print() override {
        // Print document
    }
    
    void serialize() override {
        // Serialize document
    }
    
    int get_pages() {
        return page_count;  // ✅ Class methods can access private state
    }
};

// ============ Improvement 5: Access control checks ============
class BankAccount {
private:
    double balance;      // ✅ private: only this class can access
    
protected:
    int account_id;      // ✅ protected: this class and derived classes can access
    
public:
    void deposit(double amount) {
        balance += amount;  // ✅ Correct: this class can access private state
    }
    
    double get_balance() {
        return balance;  // ✅ Correct: accessed through a public method
    }
};

class SavingsAccount : public BankAccount {
public:
    void set_id(int id) {
        account_id = id;  // ✅ Correct: derived class can access protected
        // balance = 0;   // ❌ Incorrect: derived class cannot access base private state
    }
};

// ============ Integrated tests ============
int main() {
    // ✅ new expression (calls constructor, initializes vtable)
    Shape* shape = new Rectangle(10, 20);
    
    // ✅ Virtual function call (via vtable)
    int area = shape->area();
    
    // ✅ Polymorphism
    int perim = shape->perimeter();
    
    // ✅ delete expression (calls destructor, releases memory)
    delete shape;
    
    // ✅ Multiple inheritance
    Document* doc = new Document();
    doc->print();
    doc->serialize();
    delete doc;
    
    // ✅ Access control
    BankAccount* account = new BankAccount();
    account->deposit(100);  // ✅ Through a public method
    // account->balance += 50;  // ❌ Compile error: private member is inaccessible
    delete account;
    
    return 0;
}
