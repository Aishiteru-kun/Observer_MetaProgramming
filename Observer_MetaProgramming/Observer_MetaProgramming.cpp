#include <algorithm>
#include <iostream>

#include "DelegateInstance.h"

using FOnHealthChanged = Delegates::TMulticastDelegate<void(int, int, int)>;

struct Logger
{
    void Update(int MaxHealth, int Health, int Delta)
    {
        std::cout << "Logger Update: MaxHealth = " << MaxHealth << ", Health = " << Health << ", Delta = " << Delta << std::endl;
    }
};

struct HUD
{
    void Update(int MaxHealth, int Health, int Delta)
    {
        std::cout << "HUD Update: MaxHealth = " << MaxHealth << ", Health = " << Health << ", Delta = " << Delta << std::endl;
    }
};

struct Entity
{
    FOnHealthChanged OnHealthChanged;

    void ApplyHealthChanged(int InDelta)
    {
        Health = std::clamp<int>(Health + InDelta, 0, MaxHealth);
        OnHealthChanged.Broadcast(MaxHealth, Health, InDelta);
    }

private:
    int MaxHealth = 100;
    int Health = 100;
};

int main(int argc, char* argv[])
{
    Entity Player;

    Logger Log;
    HUD Hud;

    Player.OnHealthChanged.AddRaw(&Log, &Logger::Update);
    Player.OnHealthChanged.AddRaw(&Hud, &HUD::Update);

    Player.ApplyHealthChanged(-50);
    Player.ApplyHealthChanged(10);

    std::cout << "\n";

    Player.OnHealthChanged.RemoveAll(&Log);

    Player.ApplyHealthChanged(10);

    std::cout << "\n";

    const Delegates::FDelegateHandle Handle = Player.OnHealthChanged.AddRaw(&Log, &Logger::Update);

    Player.ApplyHealthChanged(10);

    std::cout << "\n";

    Player.OnHealthChanged.Remove(Handle);

    Player.ApplyHealthChanged(10);

    std::cout << "\n";

    return 0;
}
