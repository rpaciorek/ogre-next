/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-present Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreVulkanDevice.h"

#include "OgreException.h"
#include "OgreStringConverter.h"

#include <vulkan/vulkan.h>

namespace Ogre
{
    VulkanDevice::VulkanDevice( VkInstance instance, uint32 deviceIdx ) :
        mInstance( instance ),
        mPhysicalDevice( 0 ),
        mDevice( 0 )
    {
        createPhysicalDevice( deviceIdx );
    }
    //-------------------------------------------------------------------------
    VkInstance VulkanDevice::createInstance( const String &appName,
                                             const FastArray<const char *> &extensions )
    {
        VkInstanceCreateInfo createInfo;
        VkApplicationInfo appInfo;
        memset( &createInfo, 0, sizeof( createInfo ) );
        memset( &appInfo, 0, sizeof( appInfo ) );

        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = appName.c_str();
        appInfo.pEngineName = "Ogre3D Vulkan Engine";
        appInfo.engineVersion = OGRE_VERSION;
        appInfo.apiVersion = VK_MAKE_VERSION( 1, 0, 2 );

        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        createInfo.enabledExtensionCount = static_cast<uint32>( extensions.size() );
        createInfo.ppEnabledExtensionNames = extensions.begin();

        VkInstance instance;
        vkCreateInstance( &createInfo, 0, &instance );

        return instance;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::createPhysicalDevice( uint32 deviceIdx )
    {
        // Note multiple GPUs may be present, and there may be multiple drivers for
        // each GPU hence the number of devices can theoretically get really high
        const uint32_t c_maxDevices = 64u;
        uint32 numDevices;
        vkEnumeratePhysicalDevices( mInstance, &numDevices, NULL );

        if( numDevices == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "No Vulkan devices found.",
                         "VulkanDevice::createDevice" );
        }

        numDevices = std::min( numDevices, c_maxDevices );

        const String numDevicesStr = StringConverter::toString( numDevices );
        String deviceIdsStr = StringConverter::toString( deviceIdx );

        LogManager::getSingleton().logMessage( "[Vulkan] Found " + numDevicesStr + " devices" );

        if( deviceIdx >= numDevices )
        {
            LogManager::getSingleton().logMessage( "[Vulkan] Requested device index " + deviceIdsStr +
                                                   " but there's only " +
                                                   StringConverter::toString( numDevices ) + "devices" );
            deviceIdx = 0u;
            deviceIdsStr = "0";
        }

        LogManager::getSingleton().logMessage( "[Vulkan] Selecting device " + deviceIdsStr );

        VkPhysicalDevice pd[c_maxDevices];
        vkEnumeratePhysicalDevices( mInstance, &numDevices, pd );
        mPhysicalDevice = pd[0];

        vkGetPhysicalDeviceMemoryProperties( mPhysicalDevice, &mMemoryProperties );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::calculateQueueIdx( QueueFamily family )
    {
        uint32 queueIdx = 0u;
        for( size_t i = 0u; i < family; ++i )
        {
            if( mSelectedQueues[family].familyIdx == mSelectedQueues[i].familyIdx )
                ++queueIdx;
        }
        const size_t familyIdx = mSelectedQueues[family].familyIdx;
        queueIdx = std::min( queueIdx, mQueueProps[familyIdx].queueCount - 1u );
        mSelectedQueues[family].queueIdx = queueIdx;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::fillQueueSelectionData( VkDeviceQueueCreateInfo *outQueueCreateInfo,
                                               uint32 &outNumQueues )
    {
        uint32 numQueues = 0u;

        bool queueInserted[NumQueueFamilies];
        for( size_t i = 0u; i < NumQueueFamilies; ++i )
            queueInserted[i] = false;

        for( size_t i = 1u; i < NumQueueFamilies; ++i )
            calculateQueueIdx( static_cast<QueueFamily>( i ) );

        for( size_t i = 0u; i < NumQueueFamilies; ++i )
        {
            if( !queueInserted[i] )
            {
                outQueueCreateInfo[numQueues].queueFamilyIndex = mSelectedQueues[i].familyIdx;
                outQueueCreateInfo[numQueues].queueCount = 1u;
                queueInserted[i] = true;
                for( size_t j = i + 1u; j < NumQueueFamilies; ++j )
                {
                    if( mSelectedQueues[i].familyIdx == mSelectedQueues[j].familyIdx )
                    {
                        outQueueCreateInfo[numQueues].queueCount =
                            std::max( mSelectedQueues[i].queueIdx, mSelectedQueues[j].queueIdx ) + 1u;
                        queueInserted[j] = true;
                    }
                }
                ++numQueues;
            }
        }

        outNumQueues = numQueues;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::createDevice( FastArray<const char *> &extensions )
    {
        uint32 numQueues;
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, NULL );
        if( numQueues == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Vulkan device is reporting 0 queues!",
                         "VulkanDevice::createDevice" );
        }
        mQueueProps.resize( numQueues );
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, &mQueueProps[0] );

        for( size_t i = 0; i < NumQueueFamilies; ++i )
            mSelectedQueues[i] = SelectedQueue();

        for( uint32 i = 0u; i < numQueues; ++i )
        {
            if( ( mQueueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) &&
                !mSelectedQueues[Graphics].hasValidFamily() )
            {
                mSelectedQueues[Graphics].familyIdx = i;
            }

            if( ( mQueueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT ) )
            {
                // Prefer *not* sharing compute and graphics in the same Queue
                // Note some GPUs may advertise a queue family that has both
                // graphics & compute and support multiples queues. That's fine!
                const uint32 familyIdx = mSelectedQueues[Compute].familyIdx;
                if( !mSelectedQueues[Compute].hasValidFamily() ||
                    ( ( mQueueProps[familyIdx].queueFlags & VK_QUEUE_GRAPHICS_BIT ) &&
                      mQueueProps[familyIdx].queueCount == 1u ) )
                {
                    mSelectedQueues[Compute].familyIdx = i;
                }
            }

            if( ( mQueueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT ) )
            {
                // Prefer the transfer queue that doesn't share with anything else!
                const uint32 familyIdx = mSelectedQueues[Transfer].familyIdx;
                if( !mSelectedQueues[Transfer].hasValidFamily() ||
                    !( mQueueProps[familyIdx].queueFlags & ( uint32 )( ~VK_QUEUE_TRANSFER_BIT ) ) )
                {
                    mSelectedQueues[Transfer].familyIdx = i;
                }
            }
        }

        // Graphics and Compute queues are implicitly Transfer; and drivers are
        // not required to advertise the transfer bit on those queues.
        if( !mSelectedQueues[Transfer].hasValidFamily() )
            mSelectedQueues[Transfer] = mSelectedQueues[Graphics];

        uint32 numQueuesToCreate = 0u;
        VkDeviceQueueCreateInfo queueCreateInfo[NumQueueFamilies];
        memset( &queueCreateInfo, 0, sizeof( queueCreateInfo ) );
        for( size_t i = 0; i < NumQueueFamilies; ++i )
            queueCreateInfo[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        fillQueueSelectionData( queueCreateInfo, numQueuesToCreate );

        extensions.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

        VkDeviceCreateInfo createInfo;
        memset( &createInfo, 0, sizeof( createInfo ) );

        createInfo.enabledExtensionCount = static_cast<uint32>( extensions.size() );
        createInfo.ppEnabledExtensionNames = extensions.begin();

        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = numQueuesToCreate;
        createInfo.pQueueCreateInfos = queueCreateInfo;

        vkCreateDevice( mPhysicalDevice, &createInfo, NULL, &mDevice );

        for( uint32 i = 0u; i < NumQueueFamilies; ++i )
        {
            if( mSelectedQueues[i].hasValidFamily() )
            {
                vkGetDeviceQueue( mDevice, mSelectedQueues[i].familyIdx, mSelectedQueues[i].queueIdx,
                                  &mQueues[i] );
            }
        }
    }
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    VulkanDevice::SelectedQueue::SelectedQueue() :
        familyIdx( std::numeric_limits<uint32>::max() ),
        queueIdx( 0 )
    {
    }
    //-------------------------------------------------------------------------
    bool VulkanDevice::SelectedQueue::hasValidFamily( void ) const
    {
        return familyIdx != std::numeric_limits<uint32>::max();
    }
}  // namespace Ogre