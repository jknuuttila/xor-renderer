#pragma once

#include "Core/Core.hpp"
#include "Xor/XorBackend.hpp"

#include <type_traits>

namespace xor
{
    struct ConfigGroup {};
    struct ConfigWindow {};
    struct ConfigSlider {};
    struct ConfigInput {};

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
        virtual bool customUpdate() { return false; }

        bool configure()
        {
            m_changed = update();
            m_changed |= customUpdate();
            return changed();
        }

        virtual std::vector<Configurable *> *configurableMembers() { return nullptr; }
    };

    template <typename Self, typename StructType, const char *(*Name)(), int WindowX = -1, int WindowY = -1>
    class ConfigStruct : public Configurable
    {
        std::vector<Configurable *> m_configurableMembers;
    public:
        ConfigStruct()
            : Configurable(Name(),
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

        std::vector<Configurable *> *configurableMembers() override { return &m_configurableMembers; }

    private:
        bool beginStruct(ConfigWindow)
        {
            if (WindowX >= 0 && WindowY >= 0)
                ImGui::SetNextWindowPos(ImVec2(WindowX, WindowY), ImGuiSetCond_Appearing);
            return ImGui::Begin(name(), nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        }

        void endStruct(ConfigWindow)
        {
            ImGui::End();
        }

        bool beginStruct(ConfigGroup)
        {
            ImGui::NewLine();
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

        ConfigValue &operator=(T value)
        {
            m_value = value;
            return *this;
        }

        T min() const { return static_cast<T>(m_min); }
        T max() const { return static_cast<T>(m_max); }

        bool update() override
        {
            return update(m_value, ControlType {});
        }
    private:
        bool update(bool &v, ConfigSlider) { return ImGui::Checkbox(name(), &v); }
        bool update(bool &v, ConfigInput) { return ImGui::Checkbox(name(), &v); }

        bool update(int  &v, ConfigSlider) { return ImGui::SliderInt(name(), &v, min(), max()); }
        bool update(int2 &v, ConfigSlider) { return ImGui::SliderInt2(name(), v.data(), min(), max()); }
        bool update(int3 &v, ConfigSlider) { return ImGui::SliderInt3(name(), v.data(), min(), max()); }
        bool update(int4 &v, ConfigSlider) { return ImGui::SliderInt4(name(), v.data(), min(), max()); }

        bool update(float  &v, ConfigSlider) { return ImGui::SliderFloat(name(), &v, min(), max()); }
        bool update(float2 &v, ConfigSlider) { return ImGui::SliderFloat2(name(), v.data(), min(), max()); }
        bool update(float3 &v, ConfigSlider) { return ImGui::SliderFloat3(name(), v.data(), min(), max()); }
        bool update(float4 &v, ConfigSlider) { return ImGui::SliderFloat4(name(), v.data(), min(), max()); }

        bool update(int  &v, ConfigInput) { return ImGui::InputInt(name(), &v, min(), max()); }
        bool update(int2 &v, ConfigInput) { return ImGui::InputInt2(name(), v.data(), min(), max()); }
        bool update(int3 &v, ConfigInput) { return ImGui::InputInt3(name(), v.data(), min(), max()); }
        bool update(int4 &v, ConfigInput) { return ImGui::InputInt4(name(), v.data(), min(), max()); }

        bool update(float  &v, ConfigInput) { return ImGui::InputFloat(name(), &v, min(), max()); }
        bool update(float2 &v, ConfigInput) { return ImGui::InputFloat2(name(), v.data(), min(), max()); }
        bool update(float3 &v, ConfigInput) { return ImGui::InputFloat3(name(), v.data(), min(), max()); }
        bool update(float4 &v, ConfigInput) { return ImGui::InputFloat4(name(), v.data(), min(), max()); }
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

        ConfigEnum &operator=(T value)
        {
            m_value = value;
            return *this;
        }

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

    class ConfigText final : public Configurable
    {
        std::function<void()> m_update;
    public:
        template <typename T, typename MemFn>
        ConfigText(const char *label, const char *fmt, T *cfgStruct, MemFn f)
            : Configurable(label, this, this + 1)
        {
            m_update = [=] ()
            {
                ImGui::LabelText(label, fmt, (cfgStruct->*f)());
            };
        }

        bool update() override
        {
            m_update();
            return false;
        }
    };

    class ConfigSeparator final : public Configurable
    {
    public:
        ConfigSeparator()
            : Configurable(nullptr, this, this + 1)
        {}

        bool update() override
        {
            ImGui::Separator();
            return false;
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

#define XOR_CONFIG_STRUCT(TypeName, StructType, NameFnQualifier, ...) \
    struct TypeName; \
    NameFnQualifier const char *xorConfigWindowName_ ## TypeName() { return #TypeName ; } \
    struct TypeName : public ::xor::ConfigStruct<TypeName, StructType, &xorConfigWindowName_ ## TypeName, ## __VA_ARGS__>

#define XOR_CONFIG_WINDOW(TypeName, ...) XOR_CONFIG_STRUCT(TypeName, ::xor::ConfigWindow, inline, ## __VA_ARGS__)
#define XOR_CONFIG_GROUP(TypeName) XOR_CONFIG_STRUCT(TypeName, ::xor::ConfigGroup, static)

#define XOR_CONFIG_ENUM(EnumType, ValueName, DefaultValue) \
    ::xor::ConfigEnum<EnumType> ValueName { #ValueName, DefaultValue };
#define XOR_CONFIG_CHECKBOX(ValueName, DefaultValue) \
    ::xor::ConfigValue<bool> ValueName { #ValueName, DefaultValue };
#define XOR_CONFIG_SLIDER(Type, ValueName, DefaultValue, ...) \
    ::xor::ConfigValue<Type, ::xor::ConfigSlider> ValueName { #ValueName, DefaultValue, ## __VA_ARGS__ };
#define XOR_CONFIG_INPUT(Type, ValueName, DefaultValue, ...) \
    ::xor::ConfigValue<Type, ::xor::ConfigInput> ValueName { #ValueName, DefaultValue, ## __VA_ARGS__ };
#define XOR_CONFIG_TEXT(Label, FmtString, ValueMemberFunction) \
    ::xor::ConfigText XOR_CONCAT(xorConfigText, __COUNTER__) { Label, FmtString, this, ValueMemberFunction };
#define XOR_CONFIG_SEPARATOR ::xor::ConfigSeparator XOR_CONCAT(xorConfigSeparator, __COUNTER__);
