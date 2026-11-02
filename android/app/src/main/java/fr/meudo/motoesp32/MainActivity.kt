package fr.meudo.motoesp32

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.ContextCompat
import fr.meudo.motoesp32.ui.AppliMoto
import fr.meudo.motoesp32.ui.ThemeMoto

class MainActivity : ComponentActivity() {

    private val viewModel: MotoViewModel by viewModels()

    private var permissionsAccordees by mutableStateOf(false)

    private val permissionsNecessaires: Array<String> =
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT)
        } else {
            arrayOf(Manifest.permission.ACCESS_FINE_LOCATION)
        }

    private val demandePermissions =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { resultats ->
            permissionsAccordees = resultats.values.all { it } || verifierPermissions()
        }

    private val demandeActivationBluetooth =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        permissionsAccordees = verifierPermissions()

        setContent {
            ThemeMoto {
                AppliMoto(
                    viewModel = viewModel,
                    permissionsAccordees = permissionsAccordees,
                    demanderPermissions = { demandePermissions.launch(permissionsNecessaires) },
                    activerBluetooth = {
                        demandeActivationBluetooth.launch(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                    },
                )
            }
        }
    }

    override fun onResume() {
        super.onResume()
        permissionsAccordees = verifierPermissions()
    }

    private fun verifierPermissions(): Boolean = permissionsNecessaires.all {
        ContextCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }
}
