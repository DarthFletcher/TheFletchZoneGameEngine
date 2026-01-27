#pragma once
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <stdexcept>
#include <type_traits>

class Component;

class Entity {
public:
    template<typename T, typename... Args>
    T& AddComponent(Args&&... args)
    {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Component");
        if (components.find(typeid(T)) != components.end())
            throw std::runtime_error("Component already exists!");

        auto up = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *up;
        components[typeid(T)] = std::move(up);
        return ref;
    }

    template<typename T>
    T* GetComponent()
    {
        auto it = components.find(typeid(T));
        if (it != components.end())
            return static_cast<T*>(it->second.get());
        return nullptr;
    }

    template<typename T>
    void RemoveComponent()
    {
        components.erase(typeid(T));
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;
};

class Component {
public:
    virtual ~Component() = default;
};
