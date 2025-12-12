/*  LGPE Rare Candy
 *
 *  From: https://github.com/PokemonAutomation/
 *
 */

#ifndef PokemonAutomation_PokemonLGPE_RareCandy_H
#define PokemonAutomation_PokemonLGPE_RareCandy_H

#include "NintendoSwitch/NintendoSwitch_SingleSwitchProgram.h"
#include "CommonFramework/Notifications/EventNotificationsTable.h"
#include "Common/Cpp/Options/SimpleIntegerOption.h"

namespace PokemonAutomation{
namespace NintendoSwitch{
namespace PokemonLGPE{

class RareCandy_Descriptor : public SingleSwitchProgramDescriptor{
public:
    RareCandy_Descriptor();
};

class RareCandy : public SingleSwitchProgramInstance{
public:
    RareCandy();
    virtual void program(SingleSwitchProgramEnvironment& env, CancellableScope& scope) override;

private:
    SimpleIntegerOption<uint32_t> REPEAT_COUNT;
    
    EventNotificationOption NOTIFICATION_STATUS_UPDATE;
    EventNotificationsOption NOTIFICATIONS;
};




}
}
}
#endif
