#pragma once

#include "Core/Core.hpp"
#include "Xor/XorBackend.hpp"

#include <type_traits>

namespace xor
{
    struct ConfigGroup {};
    struct ConfigWindow {};
    struct ConfigSlider {};

    class Configurable;
    void registerConfigurable(Configurable *cfg, const void *addrBegin, const void *addrEnd);
    void unregisterConfigurable(Configurable *cfg);
    void processConfigurables();

    class Configurable
    {
        const char *m_name = nullptr;
        bool m_changed = false;
    public:
        Configurable(const char *name, const void *myBegin, const void *myEnd) : m_name(name)
        {
            registerConfigurable(this, myBegin, myEnd);
        }

        virtual ~Configurable()
        {
            unregisterConfigurable(this);
        }

        const char *name() const { return m_name; }

        bool changed() const { return m_changed; }

        virtual bool update() = 0;
        virtual bool customUpdate() {}

        bool configure()
        {
            m_changed = update();
            m_changed |= customUpdate();
            return changed();
        }

        virtual std::vector<Configurable *> *configurableMembers() { return nullptr; }
    };

    template <typename Self, typename StructType = ConfigWindow>
    class ConfigStruct : public Configurable
    {
        std::vector<Configurable *> m_configurableMembers;
    public:
        ConfigStruct()
            : Configurable(xorConfigWindowName(*static_cast<Self *>(this)),
                           static_cast<Self *>(this),
                           static_cast<Self *>(this) + 1)
        {}

        bool update() override
        {
            bool changed = false;

            if (beginStruct(StructType {}))
            {
                for (auto &m : m_configurableMembers)
                    changed |= m->configure();
            }
            endStruct(StructType {});

            return changed;
        }

    private:
        bool beginStruct(ConfigWindow)
        {
            return ImGui::Begin(name(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        }

        void endStruct(ConfigWindow)
        {
            ImGui::End();
        }

        bool beginStruct(ConfigGroup)
        {
            ImGui::BeginGroup();
            ImGui::Text("%s", name());
            ImGui::Indent();
            return true;
        }

        void endStruct(ConfigGroup)
        {
            ImGui::Unindent();
            ImGui::EndGroup();
        }
    };
    template <typename T, typename ControlType = ConfigSlider>
    class ConfigValue final : public Configurable
    {
        T m_value = static_cast<T>(0);
        double m_min = 0;
        double m_max = 0;
    public:
        ConfigValue(const char *name, T initialValue, double minValue = 0, double maxValue = 0)
            : Configurable(name, this, this + 1)
            , m_value(initialValue)
            , m_min(minValue)
            , m_max(maxValue)
        {
            if (minValue == 0 && maxValue == 0)
            {
                m_min = 0;

                if (std::is_floating_point<T>::value)
                    m_max = 1;
                else
                    m_max = std::numeric_limits<T>::max();
            }
        }

        T get() const { return m_value; }
        operator T() const { return get(); }

        T min() const { return static_cast<T>(m_min); }
        T max() const { return static_cast<T>(m_max); }

        bool update() override
        {
            return update(m_value, ControlType {});
        }
    private:
        bool update(bool &v, ConfigSlider) { return ImGui::Checkbox(name(), &v); }

        bool update(int  &v, ConfigSlider) { return ImGui::SliderInt(name(), &v, min(), max()); }
        bool update(int2 &v, ConfigSlider) { return ImGui::SliderInt2(name(), v.data(), min(), max()); }
        bool update(int3 &v, ConfigSlider) { return ImGui::SliderInt3(name(), v.data(), min(), max()); }
        bool update(int4 &v, ConfigSlider) { return ImGui::SliderInt4(name(), v.data(), min(), max()); }

        bool update(float  &v, ConfigSlider) { return ImGui::SliderFloat(name(), &v, min(), max()); }
        bool update(float2 &v, ConfigSlider) { return ImGui::SliderFloat2(name(), v.data(), min(), max()); }
        bool update(float3 &v, ConfigSlider) { return ImGui::SliderFloat3(name(), v.data(), min(), max()); }
        bool update(float4 &v, ConfigSlider) { return ImGui::SliderFloat4(name(), v.data(), min(), max()); }
    };

    std::vector<char>   determineConfigEnumValueNamesZeroSeparated(const char *stringizedMacroVarags);
    std::vector<String> determineConfigEnumValueNames(const char *stringizedMacroVarags);

    template <typename T>
    class ConfigEnum final : public Configurable
    {
        T m_value;
    public:
        ConfigEnum(const char *name, T initialValue)
            : Configurable(name, this, this + 1)
            , m_value(initialValue)
        {}

        T get() const { return m_value; }
        operator T() const { return get(); }

        const char *valueName() const
        {
            return xorConfigEnumValueName(m_value);
        }

        bool update() override
        {
            return ImGui::Combo(name(),
                                reinterpret_cast<int *>(&m_value),
                                xorConfigEnumValueNamesZeroSeparated(m_value));
        }
    };
}

#define XOR_DEFINE_CONFIG_ENUM(EnumType, ...) \
    enum class EnumType { __VA_ARGS__ }; \
    static const char *xorConfigEnumValueNamesZeroSeparated(const EnumType &) \
    { \
        static auto enumValueNames = ::xor::determineConfigEnumValueNamesZeroSeparated(# __VA_ARGS__); \
        return enumValueNames.data(); \
    } \
    static const char *xorConfigEnumValueName(const EnumType &e) \
    { \
        static auto enumValueNames = ::xor::determineConfigEnumValueNames(# __VA_ARGS__); \
        return enumValueNames[static_cast<int>(e)].cStr(); \
    }

#define XOR_CONFIG_STRUCT(TypeName, StructType) \
    struct TypeName; \
    static const char *xorConfigWindowName(const TypeName &) { return #TypeName ; } \
    struct TypeName : public ::xor::ConfigStruct<TypeName, StructType>

#define XOR_CONFIG_WINDOW(TypeName) XOR_CONFIG_STRUCT(TypeName, ::xor::ConfigWindow)
#define XOR_CONFIG_GROUP(TypeName) XOR_CONFIG_STRUCT(TypeName, ::xor::ConfigGroup)

#define XOR_CONFIG_ENUM(EnumType, ValueName, DefaultValue) \
    ::xor::ConfigEnum<EnumType> ValueName { #ValueName, DefaultValue };
#define XOR_CONFIG_ENUM_D(EnumType, ValueName, DefaultValue, ...) \
    XOR_DEFINE_CONFIG_ENUM(EnumType, ## __VA_ARGS__) \
    XOR_CONFIG_ENUM(EnumType, ValueName, DefaultValue)
#define XOR_CONFIG_SLIDER(Type, ValueName, DefaultValue, ...) \
#define XOR_CONFIG_SLIDER(Type, ValueName, DefaultValue, ...) \
    ::xor::ConfigValue<Type, ::xor::ConfigSlider> ValueName { #ValueName, DefaultValue, ## __VA_ARGS__ };
