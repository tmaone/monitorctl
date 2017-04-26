//
//  DDC.c
//  DDC Panel
//
//  Created by Jonathan Taylor on 7/10/09.
//  See http://github.com/jontaylor/DDC-CI-Tools-for-OS-X
//

#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <ApplicationServices/ApplicationServices.h>
#include "DDC.h"

#define kMaxRequests 10

dispatch_semaphore_t DisplayQueue(CGDirectDisplayID displayID) {
    static UInt64 queueCount = 0;
    static struct DDCQueue {CGDirectDisplayID id; dispatch_semaphore_t queue;} *queues = NULL;
    dispatch_semaphore_t queue = NULL;
    if (!queues)
    queues = calloc(50, sizeof(*queues)); //FIXME: specify
    UInt64 i = 0;
    while (i < queueCount)
    if (queues[i].id == displayID)
    break;
    else
    i++;
    if (queues[i].id == displayID)
    queue = queues[i].queue;
    else
    queues[queueCount++] = (struct DDCQueue){displayID, (queue = dispatch_semaphore_create(1))};
    return queue;
}

bool requestFrameBuffers(CGDirectDisplayID displayID,  IOI2CRequest *request, UInt64 displayNum){

    dispatch_semaphore_t queue = DisplayQueue(displayID);
    dispatch_semaphore_wait(queue, DISPATCH_TIME_FOREVER);
    bool result;
    IOItemCount busCount;
    UInt64 fdisp = 0;

    io_service_t framebuffer; // https://developer.apple.com/reference/kernel/ioframebuffer
    io_iterator_t iter;
    io_service_t serv, servicePort = 0;

    kern_return_t err = IOServiceGetMatchingServices(kIOMasterPortDefault, IOServiceMatching(IOFRAMEBUFFER_CONFORMSTO), &iter);
    if (err != KERN_SUCCESS) return 0;
    // now recurse the IOReg tree

    while ((serv = IOIteratorNext(iter)) != MACH_PORT_NULL)
    {
        CFDictionaryRef info;
        io_name_t	name;
        CFIndex vendorID = 0, productID = 0, serialNumber = 0;
        CFNumberRef vendorIDRef, productIDRef, serialNumberRef;
        CFStringRef location = CFSTR("");
        CFStringRef serial = CFSTR("");
        Boolean success = 0;

        // get metadata from IOreg node
        IORegistryEntryGetName(serv, name);
        info = IODisplayCreateInfoDictionary(serv, kIODisplayOnlyPreferredName);

        if (CFDictionaryGetValueIfPresent(info, CFSTR(kDisplayVendorID), (const void**)&vendorIDRef))
        success = CFNumberGetValue(vendorIDRef, kCFNumberCFIndexType, &vendorID);


        if (CFDictionaryGetValueIfPresent(info, CFSTR(kDisplayProductID), (const void**)&productIDRef))
        success &= CFNumberGetValue(productIDRef, kCFNumberCFIndexType, &productID);

        IOItemCount busCount;
        IOFBGetI2CInterfaceCount(serv, &busCount);

        if (!success || busCount < 1 || CGDisplayIsBuiltin(displayID)) {
            // this does not seem to be a DDC-enabled display, skip it
            CFRelease(info);
            continue;
        }

        if (CFDictionaryGetValueIfPresent(info, CFSTR(kDisplaySerialNumber), (const void**)&serialNumberRef))
        CFNumberGetValue(serialNumberRef, kCFNumberCFIndexType, &serialNumber);

        // compare IOreg's metadata to CGDisplay's metadata to infer if the IOReg's I2C monitor is the display for the given NSScreen.displayID
        if (CGDisplayVendorNumber(displayID) != vendorID  ||
        CGDisplayModelNumber(displayID)  != productID ||
        CGDisplaySerialNumber(displayID) != serialNumber) // SN is zero in lots of cases, so duplicate-monitors can confuse us :-/
        {
            CFRelease(info);
            continue;
        }

        fdisp ++;

        if ( fdisp != displayNum ){
            CFRelease(info);
            continue;
        }
        usleep(20000);

        servicePort = serv;
        framebuffer = serv;
        CFRelease(info);
        // break;
        result = false;

        if (IOFBGetI2CInterfaceCount(framebuffer, &busCount) == KERN_SUCCESS) {
            IOOptionBits bus = 0;

            while (bus < busCount) {
                io_service_t interface;
                if (IOFBCopyI2CInterfaceForBus(framebuffer, bus++, &interface) != KERN_SUCCESS)
                continue;
                // usleep(20000);
                IOI2CConnectRef connect;
                if (IOI2CInterfaceOpen(interface, kNilOptions, &connect) == KERN_SUCCESS) {
                    result = (IOI2CSendRequest(connect, kNilOptions, request) == KERN_SUCCESS);
                    IOI2CInterfaceClose(connect, kNilOptions);
                }
                // usleep(20000);
                IOObjectRelease(interface);
                if (result) break;
            }
        }
        IOObjectRelease(framebuffer);
        if (request->replyTransactionType == kIOI2CNoTransactionType)
        usleep(20000);
        dispatch_semaphore_signal(queue);
        break;
    }
    IOObjectRelease(iter);
    return result && request->result == KERN_SUCCESS;
}

bool DisplayRequest(CGDirectDisplayID display,  IOI2CRequest *request, UInt64 displayNum) {
    return requestFrameBuffers(display, request, displayNum);
}

bool DDCWrite(CGDirectDisplayID display, struct DDCWriteCommand *write, UInt64 displayNum) {
    IOI2CRequest    request;
    UInt8           data[128];

    bzero( &request, sizeof(request));

    request.commFlags                       = 0;

    request.sendAddress                     = 0x6E;
    request.sendTransactionType             = kIOI2CSimpleTransactionType;
    request.sendBuffer                      = (vm_address_t) &data[0];
    request.sendBytes                       = 7;

    data[0] = 0x51;
    data[1] = 0x84;
    data[2] = 0x03;
    data[3] = write->control_id;
    data[4] = (write->new_value) >> 8;
    data[5] = write->new_value & 255;
    data[6] = 0x6E ^ data[0] ^ data[1] ^ data[2] ^ data[3]^ data[4] ^ data[5];

    request.replyTransactionType            = kIOI2CNoTransactionType;
    request.replyBytes                      = 0;

    bool result = DisplayRequest(display, &request, displayNum);
    return result;
}

UInt32 SupportedTransactionType() {
    /*
    With my setup (Intel HD4600 via displaylink to 'DELL U2515H') the original app failed to read ddc and freezes my system.
    This happens because AppleIntelFramebuffer do not support kIOI2CDDCciReplyTransactionType.
    So this version comes with a reworked ddc read function to detect the correct TransactionType.
    --SamanVDR 2016
    */

    kern_return_t   kr;
    io_iterator_t   io_objects;
    io_service_t    io_service;

    kr = IOServiceGetMatchingServices(kIOMasterPortDefault,
        IOServiceNameMatching("IOFramebufferI2CInterface"), &io_objects);

        if (kr != KERN_SUCCESS) {
            printf("E: Fatal - No matching service! \n");
            return 0;
        }

        UInt32 supportedType = 0;

        while((io_service = IOIteratorNext(io_objects)) != MACH_PORT_NULL)
        {
            CFMutableDictionaryRef service_properties;
            CFIndex types = 0;
            CFNumberRef typesRef;

            kr = IORegistryEntryCreateCFProperties(io_service, &service_properties, kCFAllocatorDefault, kNilOptions);
            if (kr == KERN_SUCCESS)
            {
                if (CFDictionaryGetValueIfPresent(service_properties, CFSTR(kIOI2CTransactionTypesKey), (const void**)&typesRef))
                CFNumberGetValue(typesRef, kCFNumberCFIndexType, &types);

                /*
                We want DDCciReply but Simple is better than No-thing.
                Combined and DisplayPortNative are not useful in our case.
                */
                if (types) {
                    #ifdef DEBUG
                    printf("\nD: IOI2CTransactionTypes: 0x%02lx (%ld)\n", types, types);

                    // kIOI2CNoTransactionType = 0
                    if ( 0 == ((1 << kIOI2CNoTransactionType) & (UInt64)types)) {
                        printf("E: IOI2CNoTransactionType                   unsupported \n");
                    } else {
                        printf("D: IOI2CNoTransactionType                   supported \n");
                        supportedType = kIOI2CNoTransactionType;
                    }

                    // kIOI2CSimpleTransactionType = 1
                    if ( 0 == ((1 << kIOI2CSimpleTransactionType) & (UInt64)types)) {
                        printf("E: IOI2CSimpleTransactionType               unsupported \n");
                    } else {
                        printf("D: IOI2CSimpleTransactionType               supported \n");
                        supportedType = kIOI2CSimpleTransactionType;
                    }

                    // kIOI2CDDCciReplyTransactionType = 2
                    if ( 0 == ((1 << kIOI2CDDCciReplyTransactionType) & (UInt64)types)) {
                        printf("E: IOI2CDDCciReplyTransactionType           unsupported \n");
                    } else {
                        printf("D: IOI2CDDCciReplyTransactionType           supported \n");
                        supportedType = kIOI2CDDCciReplyTransactionType;
                    }

                    // kIOI2CCombinedTransactionType = 3
                    if ( 0 == ((1 << kIOI2CCombinedTransactionType) & (UInt64)types)) {
                        printf("E: IOI2CCombinedTransactionType             unsupported \n");
                    } else {
                        printf("D: IOI2CCombinedTransactionType             supported \n");
                        //supportedType = kIOI2CCombinedTransactionType;
                    }

                    // kIOI2CDisplayPortNativeTransactionType = 4
                    if ( 0 == ((1 << kIOI2CDisplayPortNativeTransactionType) & (UInt64)types)) {
                        printf("E: IOI2CDisplayPortNativeTransactionType    unsupported\n");
                    } else {
                        printf("D: IOI2CDisplayPortNativeTransactionType    supported \n");
                        //supportedType = kIOI2CDisplayPortNativeTransactionType;
                        // http://hackipedia.org/Hardware/video/connectors/DisplayPort/VESA%20DisplayPort%20Standard%20v1.1a.pdf
                        // http://www.electronic-products-design.com/geek-area/displays/display-port
                    }
                    #else
                    // kIOI2CSimpleTransactionType = 1
                    if ( 0 != ((1 << kIOI2CSimpleTransactionType) & (UInt64)types)) {
                        supportedType = kIOI2CSimpleTransactionType;
                    }

                    // kIOI2CDDCciReplyTransactionType = 2
                    if ( 0 != ((1 << kIOI2CDDCciReplyTransactionType) & (UInt64)types)) {
                        supportedType = kIOI2CDDCciReplyTransactionType;
                    }
                    #endif
                } else printf("E: Fatal - No supported Transaction Types! \n");

                CFRelease(service_properties);
            }

            IOObjectRelease(io_service);

            // Mac OS offers three framebuffer devices, but we can leave here
            if (supportedType > 0) return supportedType;
        }

        return supportedType;
    }
