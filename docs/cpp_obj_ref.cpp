#include <iostream>
#include <string>
#include <utility> // For std::move

class CompleteObject {
private:
    // 1. PRIVATE ENUM & ATTRIBUTES (Encapsulation)
    enum class Status { Active, Idle, Offline };
    
    std::string* data_ptr; // Heap resource (requires manual lifecycle management)
    int object_id;
    Status status;

public:
    // 2. STATIC VARIABLE (Shared across all instances)
    static int total_objects;

    // 3. DEFAULT CONSTRUCTOR
    CompleteObject() : data_ptr(new std::string("Default")), object_id(++total_objects), status(Status::Idle) {
        std::cout << "[Constructor] Default Object #" << object_id << " created.\n";
    }

    // 4. PARAMETERIZED CONSTRUCTOR WITH INITIALIZER LIST
    CompleteObject(std::string value) : data_ptr(new std::string(value)), object_id(++total_objects), status(Status::Active) {
        std::cout << "[Constructor] Custom Object #" << object_id << " ('" << *data_ptr << "') created.\n";
    }

    // 5. COPY CONSTRUCTOR (Deep Copy deep-copies heap data)
    CompleteObject(const CompleteObject& other) : data_ptr(new std::string(*other.data_ptr)), object_id(++total_objects), status(other.status) {
        std::cout << "[Copy Constructor] Copied Object #" << other.object_id << " into new Object #" << object_id << ".\n";
    }

    // 6. COPY ASSIGNMENT OPERATOR (Handles clean up and self-assignment)
    CompleteObject& operator=(const CompleteObject& other) {
        std::cout << "[Copy Assignment] Overwriting Object #" << this->object_id << " with Object #" << other.object_id << ".\n";
        if (this == &other) return *this; // Self-assignment guard
        
        *data_ptr = *other.data_ptr; // Copy data
        status = other.status;
        return *this;
    }

    // 7. MOVE CONSTRUCTOR (Steals resources from temporary objects - High Performance)
    CompleteObject(CompleteObject&& other) noexcept : data_ptr(other.data_ptr), object_id(++total_objects), status(other.status) {
        std::cout << "[Move Constructor] Moving Object #" << other.object_id << " resources to new Object #" << object_id << ".\n";
        other.data_ptr = nullptr; // Leave original object in a safe, empty state
    }

    // 8. MOVE ASSIGNMENT OPERATOR
    CompleteObject& operator=(CompleteObject&& other) noexcept {
        std::cout << "[Move Assignment] Overwriting Object #" << this->object_id << " with resources from Object #" << other.object_id << ".\n";
        if (this == &other) return *this;
        
        delete data_ptr;           // Free existing resource
        data_ptr = other.data_ptr; // Steal resource
        status = other.status;
        other.data_ptr = nullptr;  // Reset source
        return *this;
    }

    // 9. DESTRUCTOR (Cleans up heap resources to prevent memory leaks)
    ~CompleteObject() {
        if (data_ptr) {
            std::cout << "[Destructor] Destroying Object #" << object_id << " ('" << *data_ptr << "') and freeing memory.\n";
            delete data_ptr;
        } else {
            std::cout << "[Destructor] Destroying hollow/moved Object #" << object_id << ".\n";
        }
    }

    // 10. METHOD VARIATIONS (Normal, Const, Static)
    void updateData(const std::string& new_val) { // Modifies object state
        if (data_ptr) *data_ptr = new_val;
    }

    void printState() const { // 'const' guarantees this method won't change attributes
        std::cout << " -> Object ID: " << object_id 
                  << " | Data: " << (data_ptr ? *data_ptr : "NULL") 
                  << " | Total Existing: " << total_objects << "\n";
    }

    static int getTotalCount() { // Static method can be called without an object instance
        return total_objects;
    }

    // 11. OPERATOR OVERLOADING (Allows object comparison)
    bool operator==(const CompleteObject& other) const {
        return (data_ptr && other.data_ptr) ? (*data_ptr == *other.data_ptr) : false;
    }
};

// Initialize static member variable outside class execution scope
int CompleteObject::total_objects = 0;
