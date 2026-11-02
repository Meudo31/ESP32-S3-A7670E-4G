package fr.meudo.motoesp32

import android.app.Application
import android.content.Context
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import fr.meudo.motoesp32.ble.MotoBleClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MotoViewModel(application: Application) : AndroidViewModel(application) {

    val client = MotoBleClient(application, viewModelScope)

    private val preferences = application.getSharedPreferences("moto", Context.MODE_PRIVATE)

    /** Adresse du dernier module utilisé, pour se reconnecter d'un geste. */
    val dernierModule: String? get() = preferences.getString("dernier_module", null)

    fun connecter(adresse: String) {
        preferences.edit().putString("dernier_module", adresse).apply()
        client.connecter(adresse)
    }

    fun oublierModule() {
        preferences.edit().remove("dernier_module").apply()
    }

    /** Lit le fichier choisi par l'utilisateur puis lance la mise à jour. */
    fun mettreAJour(uri: Uri) {
        viewModelScope.launch {
            val octets = withContext(Dispatchers.IO) {
                try {
                    getApplication<Application>().contentResolver.openInputStream(uri)?.use { it.readBytes() }
                } catch (e: Exception) {
                    null
                }
            }
            if (octets == null) {
                client.signalerErreurOta("Lecture du fichier impossible")
            } else {
                client.mettreAJour(octets)
            }
        }
    }

    override fun onCleared() {
        client.liberer()
    }
}
