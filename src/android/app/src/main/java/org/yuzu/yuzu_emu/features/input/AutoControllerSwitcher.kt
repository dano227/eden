package org.yuzu.yuzu_emu.features.input

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import org.yuzu.yuzu_emu.utils.ParamPackage

class AutoControllerSwitcher(private val context: Context) : InputManager.InputDeviceListener {
    private val inputManager = context.getSystemService(Context.INPUT_SERVICE) as InputManager

    private val internalKeywords = listOf(
        "odin", "thor", "ayn", "xbox 360", "virtual", "gpio", "built-in", "gamepad"
    )

    fun register() {
        inputManager.registerInputDeviceListener(this, Handler(Looper.getMainLooper()))
        evaluateAndSwitchControllers()
    }

    fun unregister() {
        inputManager.unregisterInputDeviceListener(this)
    }

    override fun onInputDeviceAdded(deviceId: Int) {
        if (isGameController(deviceId)) {
            evaluateAndSwitchControllers()
        }
    }

    override fun onInputDeviceRemoved(deviceId: Int) {
        evaluateAndSwitchControllers()
    }

    override fun onInputDeviceChanged(deviceId: Int) {}

    private fun isGameController(deviceId: Int): Boolean {
        val device = InputDevice.getDevice(deviceId) ?: return false
        val sources = device.sources
        return ((sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD) ||
               ((sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK)
    }

    private fun isExternalController(device: InputDevice): Boolean {
        if (device.isExternal) return true
        val nameLower = device.name.lowercase()
        return internalKeywords.none { nameLower.contains(it) }
    }

    private fun evaluateAndSwitchControllers() {
        val controllers = inputManager.inputDeviceIds
            .mapNotNull { InputDevice.getDevice(it) }
            .filter { isGameController(it.id) }

        if (controllers.isEmpty()) return

        val targetDevice = controllers.firstOrNull { isExternalController(it) }
            ?: controllers.first()

        val registeredDevices = NativeInput.getInputDevices().map { ParamPackage(it) }
        val matchedPackage = registeredDevices.firstOrNull { pkg ->
            pkg.get("display", "").contains(targetDevice.name, ignoreCase = true)
        } ?: return

        NativeInput.loadInputProfiles()
        NativeInput.updateMappingsWithDefault(0, matchedPackage, matchedPackage)
        NativeInput.saveInputProfiles()
    }
}