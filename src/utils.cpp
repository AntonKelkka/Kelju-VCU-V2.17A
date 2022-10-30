#include "utils.h"

namespace utils
{

#define CAN_TIMEOUT       50  //500ms
#define PRECHARGE_TIMEOUT 500 //5s

float SOCVal=0;
int32_t NetWh=0;


int32_t change(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
   return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void PostErrorIfRunning(ERROR_MESSAGE_NUM err)
{
   if (Param::GetInt(Param::opmode) == MOD_RUN)
   {
      ErrorMessage::Post(err);
   }
}

void GetDigInputs(CanHardware* can)
{
   static bool canIoActive = false;
   int canio = Param::GetInt(Param::canio);

   canIoActive |= canio != 0;

   if ((rtc_get_counter_val() - can->GetLastRxTimestamp()) >= CAN_TIMEOUT && canIoActive)
   {
      canio = 0;
      Param::SetInt(Param::canio, 0);
      ErrorMessage::Post(ERR_CANTIMEOUT);
   }

   Param::SetInt(Param::din_cruise, ((canio & CAN_IO_CRUISE) != 0));
   Param::SetInt(Param::din_start, DigIo::start_in.Get() | ((canio & CAN_IO_START) != 0));
   Param::SetInt(Param::din_brake, DigIo::brake_in.Get() | ((canio & CAN_IO_BRAKE) != 0));
   Param::SetInt(Param::din_forward, DigIo::fwd_in.Get() | ((canio & CAN_IO_FWD) != 0));
   Param::SetInt(Param::din_reverse, DigIo::rev_in.Get() | ((canio & CAN_IO_REV) != 0));
   Param::SetInt(Param::din_bms, (canio & CAN_IO_BMS) != 0);
   Param::SetInt(Param::din_12Vgp, DigIo::gp_12Vin.Get());
}

float GetUserThrottleCommand()
{
   int potval, pot2val;
   bool brake = Param::GetBool(Param::din_brake);
   int potmode = Param::GetInt(Param::potmode);

   potval = AnaIn::throttle1.Get();
   pot2val = AnaIn::throttle2.Get();
   Param::SetInt(Param::pot, potval);
   Param::SetInt(Param::pot2, pot2val);


   /* Error light on implausible value */
   if (!Throttle::CheckAndLimitRange(&potval, 0))
   {
//        DigIo::err_out.Set();
      utils::PostErrorIfRunning(ERR_THROTTLE1);
      return 0;
   }

   bool throt2Res = Throttle::CheckAndLimitRange(&pot2val, 1);

   if (potmode == POTMODE_DUALCHANNEL)
   {
      if (!Throttle::CheckDualThrottle(&potval, pot2val) || !throt2Res)
      {
//            DigIo::err_out.Set();
         utils::PostErrorIfRunning(ERR_THROTTLE1);
         Param::SetInt(Param::potnom, 0);
         return 0;
      }
      pot2val = Throttle::potmax[1]; //make sure we don't attenuate regen
   }

   if (Param::GetInt(Param::dir) == 0)
      return 0;

   return Throttle::CalcThrottle(potval, pot2val, brake);
}


void SelectDirection(Vehicle* vehicle)
{
   Vehicle::gear gear;
   int8_t selectedDir = Param::GetInt(Param::dir);
   int8_t userDirSelection = 0;
   int8_t dirSign = (Param::GetInt(Param::dirmode) & DIR_REVERSED) ? -1 : 1;

   if (vehicle->GetGear(gear))
   {
      // if in an E65 we get direction from the shift stalk via CAN
      switch (gear)
      {
      case Vehicle::PARK:
         selectedDir = 0; // Park
         break;
      case Vehicle::REVERSE:
         selectedDir = -1; // Reverse
         break;
      case Vehicle::NEUTRAL:
         selectedDir = 0; // Neutral
         break;
      case Vehicle::DRIVE:
         selectedDir = 1; // Drive
         break;
      }
   }
   else
   {
      // only use this if we are NOT in an E65.
      if (Param::GetInt(Param::dirmode) == DIR_DEFAULTFORWARD)
      {
         if (Param::GetBool(Param::din_forward) && Param::GetBool(Param::din_reverse))
            selectedDir = 0;
         else if (Param::GetBool(Param::din_reverse))
            userDirSelection = -1;
         else
            userDirSelection = 1;
      }
      else if ((Param::GetInt(Param::dirmode) & 1) == DIR_BUTTON)
      {
         /* if forward AND reverse selected, force neutral, because it's charge mode */
         if (Param::GetBool(Param::din_forward) && Param::GetBool(Param::din_reverse))
            selectedDir = 0;
         else if (Param::GetBool(Param::din_forward))
            userDirSelection = 1 * dirSign;
         else if (Param::GetBool(Param::din_reverse))
            userDirSelection = -1 * dirSign;
         else
            userDirSelection = selectedDir;
      }
      else
      {
         /* neither forward nor reverse or both forward and reverse -> neutral */
         if (!(Param::GetBool(Param::din_forward) ^ Param::GetBool(Param::din_reverse)))
            selectedDir = 0;
         else if (Param::GetBool(Param::din_forward))
            userDirSelection = 1 * dirSign;
         else if (Param::GetBool(Param::din_reverse))
            userDirSelection = -1 * dirSign;
      }

      /* Only change direction when below certain motor speed */
//   if ((int)Encoder::GetSpeed() < Param::GetInt(Param::dirchrpm))
      selectedDir = userDirSelection;

      /* Current direction doesn't match selected direction -> neutral */
      if (selectedDir != userDirSelection)
         selectedDir = 0;
   }

   if (selectedDir == -1) {
     GPSet(gpout_roles::REVERSE); // Turn on reverse lights
   } else {
     GPClear(gpout_roles::REVERSE); // Turn off reverse lights
   }

   Param::SetInt(Param::dir, selectedDir);
}

float ProcessUdc(uint32_t oldTime, int motorSpeed)
{
   float udc = ((float)ISA::Voltage)/1000;//get voltage from isa sensor and post to parameter database
   Param::SetFloat(Param::udc, udc);
   float udc2 = ((float)ISA::Voltage2)/1000;//get voltage from isa sensor and post to parameter database
   Param::SetFloat(Param::udc2, udc2);
   float udc3 = ((float)ISA::Voltage3)/1000;//get voltage from isa sensor and post to parameter database
   Param::SetFloat(Param::udc3, udc3);
   float idc = ((float)ISA::Amperes)/1000;//get current from isa sensor and post to parameter database
   Param::SetFloat(Param::idc, idc);
   float kw = ((float)ISA::KW)/1000;//get power from isa sensor and post to parameter database
   Param::SetFloat(Param::power, kw);
   float kwh = ((float)ISA::KWh)/1000;//get kwh from isa sensor and post to parameter database
   Param::SetFloat(Param::KWh, kwh);
   float Amph = ((float)ISA::Ah)/3600;//get Ah from isa sensor and post to parameter database
   Param::SetFloat(Param::AMPh, Amph);
   float udclim = Param::GetFloat(Param::udclim);
   float udcsw = Param::GetFloat(Param::udcsw);

   float deltaVolts1 = (udc2 / 2) - udc3;
   float deltaVolts2 = (udc2 + udc3) - udc;
   Param::SetFloat(Param::deltaV, MAX(deltaVolts1, deltaVolts2));

   // Currently unused parameters:
   // s32fp udcmin = Param::Get(Param::udcmin);
   // s32fp udcmax = Param::Get(Param::udcmax);

   int opmode = Param::GetInt(Param::opmode);
   //Calculate "12V" supply voltage from voltage divider on mprot pin
   //1.2/(4.7+1.2)/3.33*4095 = 250 -> make it a bit less for pin losses etc
   //HW_REV1 had 3.9k resistors
   int uauxGain = 210;
   Param::SetFloat(Param::uaux, ((float)AnaIn::uaux.Get()) / uauxGain);

   if (udc > udclim)
   {
      if (ABS(motorSpeed) < 50) //If motor is stationary, over voltage comes from outside
      {
         GPClear(gpout_roles::MAIN_CON); //In this case, open DC switch
         GPClear(gpout_roles::PRECHARGE); //and

      }

      Param::SetInt(Param::opmode, MOD_OFF);
      ErrorMessage::Post(ERR_OVERVOLTAGE);
   }

   if(opmode == MOD_PRECHARGE)
   {
      if (udc < (udcsw / 2) && rtc_get_counter_val() > (oldTime+PRECHARGE_TIMEOUT) && utils::GPGet(gpout_roles::PRECHARGE))
      {
         GPClear(gpout_roles::PRECHARGE);
         ErrorMessage::Post(ERR_PRECHARGE);
         Param::SetInt(Param::opmode, MOD_PCHFAIL);
      }
   }

   return udc;
}

float ProcessThrottle(int speed)
{
   float finalSpnt;

   if (speed < Param::GetInt(Param::throtramprpm))
      Throttle::throttleRamp = Param::Get(Param::throtramp);
   else
      Throttle::throttleRamp = Param::GetAttrib(Param::throtramp)->max;

   finalSpnt = utils::GetUserThrottleCommand();

   finalSpnt = Throttle::RampThrottle(finalSpnt);

   Throttle::UdcLimitCommand(finalSpnt, Param::Get(Param::udc));
   Throttle::IdcLimitCommand(finalSpnt, Param::Get(Param::idc));
   Throttle::SpeedLimitCommand(finalSpnt, ABS(speed));

   if (Throttle::TemperatureDerate(Param::Get(Param::tmphs), Param::Get(Param::tmphsmax), finalSpnt))
   {
      ErrorMessage::Post(ERR_TMPHSMAX);
   }

   if (Throttle::TemperatureDerate(Param::Get(Param::tmpm), Param::Get(Param::tmpmmax), finalSpnt))
   {
      ErrorMessage::Post(ERR_TMPMMAX);
   }

   Param::SetFloat(Param::potnom, finalSpnt);

   return finalSpnt;
}


void displayThrottle()
{
   uint16_t potdisp = AnaIn::throttle1.Get();
   uint16_t pot2disp = AnaIn::throttle2.Get();
   Param::SetInt(Param::pot, potdisp);
   Param::SetInt(Param::pot2, pot2disp);
}


void CalcSOC()
{
   float Capacity_Parm = Param::GetFloat(Param::BattCap);
   float kwh_Used = ABS(Param::GetFloat(Param::KWh));

   SOCVal = 100.0f - 100.0f * kwh_Used / Capacity_Parm;

   if(SOCVal > 100) SOCVal = 100;
   Param::SetFloat(Param::SOC,SOCVal);
}

void GPSet(int role)
{
   if (Param::GetInt(Param::gp_out1_role) == role)
   {
      DigIo::gp_out1.Set();
   }
   if (Param::GetInt(Param::gp_out2_role) == role)
   {
      DigIo::gp_out2.Set();
   }
   if (Param::GetInt(Param::gp_out3_role) == role)
   {
      DigIo::gp_out3.Set();
   }
   if (Param::GetInt(Param::inv_out_role) == role)
   {
      DigIo::inv_out.Set();
   }
   if (Param::GetInt(Param::prec_out_role) == role)
   {
      DigIo::prec_out.Set();
   }
   if (Param::GetInt(Param::dcsw_out_role) == role)
   {
      DigIo::dcsw_out.Set();
   }
}

void GPClear(int role)
{
   if (Param::GetInt(Param::gp_out1_role) == role)
   {
      DigIo::gp_out1.Clear();
   }
   if (Param::GetInt(Param::gp_out2_role) == role)
   {
      DigIo::gp_out2.Clear();
   }
   if (Param::GetInt(Param::gp_out3_role) == role)
   {
      DigIo::gp_out3.Clear();
   }
   if (Param::GetInt(Param::inv_out_role) == role)
   {
      DigIo::inv_out.Clear();
   }
   if (Param::GetInt(Param::prec_out_role) == role)
   {
      DigIo::prec_out.Clear();
   }
   if (Param::GetInt(Param::dcsw_out_role) == role)
   {
      DigIo::dcsw_out.Clear();
   }
}

bool GPGet(int role)
{
   bool ret = false;
   if (Param::GetInt(Param::gp_out1_role) == role)
   {
      ret |= DigIo::gp_out1.Get();
   }
   if (Param::GetInt(Param::gp_out2_role) == role)
   {
      ret |= DigIo::gp_out2.Get();
   }
   if (Param::GetInt(Param::gp_out3_role) == role)
   {
      ret |= DigIo::gp_out3.Get();
   }
   if (Param::GetInt(Param::inv_out_role) == role)
   {
      ret |= DigIo::inv_out.Get();
   }
   if (Param::GetInt(Param::prec_out_role) == role)
   {
      ret |= DigIo::prec_out.Get();
   }
   if (Param::GetInt(Param::dcsw_out_role) == role)
   {
      ret |= DigIo::dcsw_out.Get();
   }
   return ret;
}

} // namespace utils
