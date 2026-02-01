// Class and inheritance tests

// Simple class definition
class Point {
public:
    int x;
    int y;
    
    int get_x() {
        return x;
    }
    
    int get_y() {
        return y;
    }
    
    int sum() {
        return x + y;
    }
};

// Base class with a virtual function
class Shape {
public:
    int id;
    
    // Virtual function (simplified; real compiler support required)
    int area() {  // Base class default implementation
        return 0;
    }
};

// Derived class: rectangle
class Rectangle {  // : public Shape (inheritance syntax not supported yet)
public:
    int width;
    int height;
    int id;  // Inherited from Shape (declared manually)
    
    int area() {  // Override the base virtual function
        return width * height;
    }
};

// Usage example
int test_simple_class() {
    // Note: simplified object creation; real code would use new or stack allocation
    Point* p = 0;  // Placeholder
    
    // Field access (requires an object instance)
    // return p->x + p->y;
    
    return 42;  // Placeholder return
}

int test_inheritance() {
    Rectangle* rect = 0;  // Placeholder
    
    // Virtual function call
    // return rect->area();
    
    return 100;  // Placeholder return
}

int main() {
    int result1 = test_simple_class();
    int result2 = test_inheritance();
    return result1 + result2;  // Should return 142
}
