//
//  ddcctl.m
//  query and control monitors through their on-wire data channels and OSD microcontrollers
//  http://en.wikipedia.org/wiki/Display_Data_Channel#DDC.2FCI
//  http://en.wikipedia.org/wiki/Monitor_Control_Command_Set
//
//  Copyright Joey Korkames 2016 http://github.com/kfix
//  Licensed under GPLv3, full text at http://www.gnu.org/licenses/gpl-3.0.txt

//  Now using argv[] instead of user-defaults to handle commandline arguments.
//  Added optional use of an external app 'OSDisplay' to have a BezelUI like OSD.
//  Have fun! Marc (Saman-VDR) 2016

#define Log(...) (void)printf("%s\n",[[NSString stringWithFormat:__VA_ARGS__] UTF8String])

#import <IOKit/graphics/IOGraphicsLib.h>
#import <Foundation/Foundation.h>
#import <AppKit/NSScreen.h>
#import "DDC.h"

NSString *EDIDString(char *string)
{
    NSString *temp = [[NSString alloc] initWithBytes:string length:13 encoding:NSASCIIStringEncoding];
    return ([temp rangeOfString:@"\n"].location != NSNotFound) ? [[temp componentsSeparatedByString:@"\n"] objectAtIndex:0] : temp;
}

void setControlAll(NSInteger control_id, uint new_value)
{

    uint32_t nDisplays;
    CGDirectDisplayID displays[0x10];
    CGGetActiveDisplayList(0x10, displays, &nDisplays);

    struct DDCWriteCommand command;
    command.control_id = control_id;
    command.new_value = new_value;
    NSString *OSDisplay = @"/Applications/OSDisplay.app/Contents/MacOS/OSDisplay";
    bool interlaced = 0;

    for (int i=0; i<nDisplays; i++)
    {
        int modeNum;
        CGSGetCurrentDisplayMode(displays[i], &modeNum);
        modes_D4 mode;
        CGSGetDisplayModeDescriptionOfLength(displays[i], modeNum, &mode, 0xD4);
        int mBitres = (mode.derived.depth == 4) ? 32 : 16;
        interlaced = ((mode.derived.flags & kDisplayModeInterlacedFlag) == kDisplayModeInterlacedFlag);

        if (interlaced)
        fprintf (stdout, "Display %d: { resolution = %dx%d,  scale = %.1f,  freq = %d,  bits/pixel = %d, interlaced }\n", i, mode.derived.width, mode.derived.height, mode.derived.density, mode.derived.freq, mBitres);
        else
        fprintf (stdout, "Display %d: { resolution = %dx%d,  scale = %.1f,  freq = %d,  bits/pixel = %d }\n", i, mode.derived.width, mode.derived.height, mode.derived.density, mode.derived.freq, mBitres);

        Log(@"D: setting  display #%u => %u", i, command.control_id, command.new_value);
    }

        if (DDCWrite(displays, nDisplays, &command)) {
            switch (control_id) {
                case 16:
                [NSTask launchedTaskWithLaunchPath:OSDisplay arguments:[NSArray arrayWithObjects: @"-l", [NSString stringWithFormat:@"%u", new_value], @"-i", @"brightness", nil]];
                break;
                case 18:
                [NSTask launchedTaskWithLaunchPath:OSDisplay arguments:[NSArray arrayWithObjects: @"-l", [NSString stringWithFormat:@"%u", new_value], @"-i", @"contrast", nil]];
                break;
                default:
                break;
            }
        }else{
            Log(@"E: Failed to send DDC command!");
        }

}

/* Main function */
int main(int argc, const char * argv[])
{

    @autoreleasepool {

        uint32_t nDisplays;
        CGDirectDisplayID displays[0x10];
        CGGetActiveDisplayList(0x10, displays, &nDisplays);

        Log(@"I: found %lu external display%@", nDisplays, nDisplays > 1 ? @"s" : @"");

        // Defaults
        NSString *screenName = @"";
        NSUInteger displayId = -1;
        NSUInteger command_interval = 100000;

        // Commandline Arguments
        NSMutableDictionary *actions = [[NSMutableDictionary alloc] init];

        for (int i=1; i<argc; i++)
        {
            if (!strcmp(argv[i], "-b")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@BRIGHTNESS, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"b"];
            }

            else if (!strcmp(argv[i], "-c")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@CONTRAST, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"c"];
            }

            else if (!strcmp(argv[i], "-rbc")) {
                [actions setObject:@[@RESET_BRIGHTNESS_AND_CONTRAST, @"1"] forKey:@"rbc"];
            }

            else if (!strcmp(argv[i], "-rg")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@RED_GAIN, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"rg"];
            }

            else if (!strcmp(argv[i], "-gg")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@GREEN_GAIN, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"gg"];
            }

            else if (!strcmp(argv[i], "-bg")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@BLUE_GAIN, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"bg"];
            }

            else if (!strcmp(argv[i], "-rrgb")) {
                [actions setObject:@[@RESET_COLOR, @"1"] forKey:@"rrgb"];
            }

            else if (!strcmp(argv[i], "-p")) {
                i++;
                if (i >= argc) break;
                [actions setObject:@[@DPMS, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"p"];
            }

            else if (!strcmp(argv[i], "-o")) { // read only
                [actions setObject:@[@ORIENTATION, @"?"] forKey:@"o"];
            }

            else if (!strcmp(argv[i], "-osd")) { // read only - returns '1' (OSD closed) or '2' (OSD active)
            [actions setObject:@[@ON_SCREEN_DISPLAY, @"?"] forKey:@"osd"];
        }

        else if (!strcmp(argv[i], "-lang")) { // read only
            [actions setObject:@[@OSD_LANGUAGE, @"?"] forKey:@"lang"];
        }

        else if (!strcmp(argv[i], "-reset")) {
            [actions setObject:@[@RESET, @"1"] forKey:@"reset"];
        }

        else if (!strcmp(argv[i], "-preset_a")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@COLOR_PRESET_A, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"preset_a"];
        }

        else if (!strcmp(argv[i], "-preset_b")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@COLOR_PRESET_B, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"preset_b"];
        }

        else if (!strcmp(argv[i], "-preset_c")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@COLOR_PRESET_C, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"preset_c"];
        }

        else if (!strcmp(argv[i], "-i")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@INPUT_SOURCE, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"i"];
        }

        else if (!strcmp(argv[i], "-m")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@AUDIO_MUTE, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"m"];
        }

        else if (!strcmp(argv[i], "-v")) {
            i++;
            if (i >= argc) break;
            [actions setObject:@[@AUDIO_SPEAKER_VOLUME, [[NSString alloc] initWithUTF8String:argv[i]]] forKey:@"v"];
        }

        else if (!strcmp(argv[i], "-w")) {
            i++;
            if (i >= argc) break;
            command_interval = atoi(argv[i]);
        }

        else {
            NSLog(@"Unknown argument: %@", [[NSString alloc] initWithUTF8String:argv[i]]);
            return -1;
        }
    }

    // Actions
    [actions enumerateKeysAndObjectsUsingBlock:^(id argname, NSArray* valueArray, BOOL *stop) {
        NSInteger control_id = [valueArray[0] intValue];
        NSString *argval = valueArray[1];
        Log(@"D: action: %@: %@", argname, argval);
        setControlAll(control_id, [argval intValue]);
        usleep(command_interval); // stagger comms to these wimpy I2C mcu's
    }];

    return 0;
}

}
