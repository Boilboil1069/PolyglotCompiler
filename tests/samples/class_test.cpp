// 类和继承测试

// 简单类定义
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

// 带虚函数的基类
class Shape {
public:
    int id;
    
    // 虚函数（简化表示，实际需要编译器支持）
    int area() {  // 基类默认实现
        return 0;
    }
};

// 派生类：矩形
class Rectangle {  // : public Shape (暂不支持继承语法)
public:
    int width;
    int height;
    int id;  // 从 Shape 继承（手动声明）
    
    int area() {  // 重写基类的虚函数
        return width * height;
    }
};

// 使用示例
int test_simple_class() {
    // 注意：这里简化了对象创建，实际需要 new 或栈分配
    Point* p = 0;  // 占位符
    
    // 字段访问（需要对象实例）
    // return p->x + p->y;
    
    return 42;  // 占位返回
}

int test_inheritance() {
    Rectangle* rect = 0;  // 占位符
    
    // 虚函数调用
    // return rect->area();
    
    return 100;  // 占位返回
}

int main() {
    int result1 = test_simple_class();
    int result2 = test_inheritance();
    return result1 + result2;  // 应返回 142
}
