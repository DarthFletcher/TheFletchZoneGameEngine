#include "Entity.h"

template<typename T, typename... Args>
void Entity::AddComponent(Args&&... args) {
    if (components.find(typeid(T)) != components.end()) {
        throw std::runtime_error("Component already exists!");
    }
    components[typeid(T)] = std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T>
T* Entity::GetComponent() {
    auto it = components.find(typeid(T));
    if (it != components.end()) {
        return static_cast<T*>(it->second.get());
    }
    return nullptr;
}

template<typename T>
void Entity::RemoveComponent() {
    components.erase(typeid(T));
}
