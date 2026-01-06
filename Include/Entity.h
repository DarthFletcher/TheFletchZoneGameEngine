#pragma once
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <stdexcept>

class Component;

class Entity {
public:
    template<typename T, typename... Args>
    void AddComponent(Args&&... args);

    template<typename T>
    T* GetComponent();

    template<typename T>
    void RemoveComponent();

private:
    std::unordered_map<std::type_index, std::unique_ptr<Component>> components;
};

class Component {
public:
    virtual ~Component() = default;
};
