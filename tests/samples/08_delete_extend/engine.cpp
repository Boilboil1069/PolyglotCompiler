// ============================================================================
// engine.cpp — C++ game engine classes for DELETE/EXTEND demo
// Compiled by PolyglotCompiler's frontend_cpp → shared IR
// ============================================================================

#include <string>
#include <vector>
#include <memory>

class GameObject {
public:
    std::string name;
    double x, y, z;
    bool active;

    GameObject(const std::string& obj_name, double px, double py, double pz)
        : name(obj_name), x(px), y(py), z(pz), active(true) {}

    ~GameObject() {
        active = false;
    }

    void move(double dx, double dy, double dz) {
        x += dx;
        y += dy;
        z += dz;
    }

    double distance_to_origin() const {
        return std::sqrt(x * x + y * y + z * z);
    }

    std::string to_string() const {
        return name + " at (" +
               std::to_string(x) + ", " +
               std::to_string(y) + ", " +
               std::to_string(z) + ")";
    }
};

class Renderer {
public:
    int width;
    int height;
    std::string backend;

    Renderer(int w, int h, const std::string& render_backend)
        : width(w), height(h), backend(render_backend) {}

    ~Renderer() {
        // Release GPU resources
    }

    void clear() {
        // Clear the frame buffer
    }

    void draw(const GameObject& obj) {
        // Draw the object
    }

    void present() {
        // Present the frame
    }

    int pixel_count() const {
        return width * height;
    }
};
