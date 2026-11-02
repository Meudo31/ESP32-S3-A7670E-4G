package fr.meudo.motoesp32.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.ParcelUuid
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeoutOrNull
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Module trouvé pendant la recherche. */
data class ModuleTrouve(val nom: String, val adresse: String, val rssi: Int)

sealed interface EtatConnexion {
    data object Deconnecte : EtatConnexion
    data object Recherche : EtatConnexion
    data class Connexion(val etape: String) : EtatConnexion
    data object Connecte : EtatConnexion
    data class Erreur(val message: String) : EtatConnexion
}

sealed interface EtatOta {
    data object Inactif : EtatOta
    data class EnCours(val progression: Float, val message: String) : EtatOta
    data object Termine : EtatOta
    data class Erreur(val message: String) : EtatOta
}

/**
 * Client Bluetooth Low Energy du module moto.
 *
 * Android n'accepte qu'une opération GATT à la fois : toutes les lectures/écritures passent par [operation]
 * et attendent le callback correspondant avant de rendre la main.
 *
 * Les permissions Bluetooth sont demandées par MainActivity avant tout appel.
 */
@SuppressLint("MissingPermission")
class MotoBleClient(private val context: Context, private val scope: CoroutineScope) {

    private val adaptateur: BluetoothAdapter? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter

    private val _etat = MutableStateFlow<EtatConnexion>(EtatConnexion.Deconnecte)
    val etat: StateFlow<EtatConnexion> = _etat.asStateFlow()

    private val _modules = MutableStateFlow<List<ModuleTrouve>>(emptyList())
    val modules: StateFlow<List<ModuleTrouve>> = _modules.asStateFlow()

    private val _telemetrie = MutableStateFlow(Telemetrie())
    val telemetrie: StateFlow<Telemetrie> = _telemetrie.asStateFlow()

    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs: StateFlow<List<String>> = _logs.asStateFlow()

    private val _reponses = MutableSharedFlow<ReponseCommande>(extraBufferCapacity = 16)
    val reponses: SharedFlow<ReponseCommande> = _reponses.asSharedFlow()

    private val _ota = MutableStateFlow<EtatOta>(EtatOta.Inactif)
    val ota: StateFlow<EtatOta> = _ota.asStateFlow()

    val bluetoothActif: Boolean get() = adaptateur?.isEnabled == true

    private var gatt: BluetoothGatt? = null
    private var mtu = 23
    private val operation = Mutex()
    private var attenteEcriture: CompletableDeferred<Int>? = null
    private var attenteDescripteur: CompletableDeferred<Int>? = null
    private var attenteMtu: CompletableDeferred<Int>? = null
    private var attenteServices: CompletableDeferred<Int>? = null
    private var attenteAppairage: CompletableDeferred<Boolean>? = null
    private val messagesOta = Channel<String>(Channel.UNLIMITED)
    private var preparation: Job? = null
    private var arretRecherche: Job? = null
    private val formatHeure = SimpleDateFormat("HH:mm:ss", Locale.FRANCE)

    // ─────────────────────────────── Recherche ───────────────────────────────

    private val rappelRecherche = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val nom = result.scanRecord?.deviceName ?: result.device.name ?: Protocole.NOM_MODULE
            val trouve = ModuleTrouve(nom, result.device.address, result.rssi)
            _modules.update { liste -> (liste.filter { it.adresse != trouve.adresse } + trouve).sortedByDescending { it.rssi } }
        }

        override fun onScanFailed(errorCode: Int) {
            _etat.value = EtatConnexion.Erreur("Recherche impossible (code $errorCode)")
        }
    }

    fun rechercher() {
        val scanner = adaptateur?.bluetoothLeScanner ?: run {
            _etat.value = EtatConnexion.Erreur("Bluetooth désactivé")
            return
        }
        arreterRecherche()
        _modules.value = emptyList()
        val filtres = listOf(
            ScanFilter.Builder().setServiceUuid(ParcelUuid(Protocole.SERVICE)).build(),
            ScanFilter.Builder().setDeviceName(Protocole.NOM_MODULE).build(),
        )
        val reglages = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        scanner.startScan(filtres, reglages, rappelRecherche)
        _etat.value = EtatConnexion.Recherche
        arretRecherche = scope.launch {
            delay(15_000)
            arreterRecherche()
        }
    }

    fun arreterRecherche() {
        arretRecherche?.cancel()
        arretRecherche = null
        try { adaptateur?.bluetoothLeScanner?.stopScan(rappelRecherche) } catch (_: Exception) { }
        if (_etat.value == EtatConnexion.Recherche) _etat.value = EtatConnexion.Deconnecte
    }

    // ─────────────────────────────── Connexion ───────────────────────────────

    fun connecter(adresse: String) {
        arreterRecherche()
        fermer()
        val appareil = try { adaptateur?.getRemoteDevice(adresse) } catch (e: IllegalArgumentException) { null }
        if (appareil == null) {
            _etat.value = EtatConnexion.Erreur("Adresse invalide")
            return
        }
        _etat.value = EtatConnexion.Connexion("Connexion…")
        gatt = appareil.connectGatt(context, false, rappelGatt, BluetoothDevice.TRANSPORT_LE)
    }

    fun deconnecter() {
        fermer()
        _etat.value = EtatConnexion.Deconnecte
    }

    private fun fermer() {
        preparation?.cancel()
        preparation = null
        gatt?.let {
            it.disconnect()
            it.close()
        }
        gatt = null
        annulerAttentes()
    }

    private fun annulerAttentes() {
        attenteEcriture?.complete(-1)
        attenteDescripteur?.complete(-1)
        attenteMtu?.complete(-1)
        attenteServices?.complete(-1)
        attenteAppairage?.complete(false)
    }

    /** Étapes après la connexion : MTU, services, appairage par code PIN, notifications. */
    private suspend fun preparer(g: BluetoothGatt) {
        _etat.value = EtatConnexion.Connexion("Négociation…")
        attenteMtu = CompletableDeferred()
        if (g.requestMtu(517)) withTimeoutOrNull(5_000) { attenteMtu?.await() }

        _etat.value = EtatConnexion.Connexion("Lecture des services…")
        attenteServices = CompletableDeferred()
        g.discoverServices()
        val statutServices = withTimeoutOrNull(10_000) { attenteServices?.await() }
        if (statutServices != BluetoothGatt.GATT_SUCCESS || g.getService(Protocole.SERVICE) == null) {
            echec("Ce module n'a pas le service de l'application (firmware à jour ?)")
            return
        }

        if (g.device.bondState != BluetoothDevice.BOND_BONDED) {
            _etat.value = EtatConnexion.Connexion("Appairage : saisissez le code PIN du module")
            if (!attendreAppairage(g.device)) {
                echec("Appairage refusé ou code PIN incorrect")
                return
            }
        }

        _etat.value = EtatConnexion.Connexion("Abonnement aux données…")
        for (uuid in listOf(Protocole.TELEMETRIE, Protocole.REPONSE, Protocole.LOGS, Protocole.OTA_CTRL)) {
            if (!activerNotifications(g, uuid)) {
                echec("Impossible de recevoir les données (appairage à refaire ?)")
                return
            }
        }
        _etat.value = EtatConnexion.Connecte
        envoyerCommande("STATUS")
    }

    private suspend fun attendreAppairage(appareil: BluetoothDevice): Boolean {
        val attente = CompletableDeferred<Boolean>()
        attenteAppairage = attente
        val recepteur = object : BroadcastReceiver() {
            override fun onReceive(c: Context, intent: Intent) {
                val concerne: BluetoothDevice? = if (Build.VERSION.SDK_INT >= 33) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION") intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                if (concerne?.address != appareil.address) return
                when (intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, BluetoothDevice.BOND_NONE)) {
                    BluetoothDevice.BOND_BONDED -> attente.complete(true)
                    BluetoothDevice.BOND_NONE -> attente.complete(false)
                }
            }
        }
        ContextCompat.registerReceiver(
            context, recepteur, IntentFilter(BluetoothDevice.ACTION_BOND_STATE_CHANGED),
            ContextCompat.RECEIVER_EXPORTED,
        )
        try {
            // Le module demande lui-même le chiffrement ; on lance l'appairage si Android ne l'a pas déjà fait.
            if (appareil.bondState == BluetoothDevice.BOND_NONE) appareil.createBond()
            if (appareil.bondState == BluetoothDevice.BOND_BONDED) return true
            return withTimeoutOrNull(60_000) { attente.await() } ?: false
        } finally {
            context.unregisterReceiver(recepteur)
            attenteAppairage = null
        }
    }

    private fun echec(message: String) {
        fermer()
        _etat.value = EtatConnexion.Erreur(message)
    }

    // ─────────────────────────────── Opérations GATT ───────────────────────────────

    private fun caracteristique(uuid: java.util.UUID): BluetoothGattCharacteristic? =
        gatt?.getService(Protocole.SERVICE)?.getCharacteristic(uuid)

    private suspend fun activerNotifications(g: BluetoothGatt, uuid: java.util.UUID): Boolean = operation.withLock {
        val car = g.getService(Protocole.SERVICE)?.getCharacteristic(uuid) ?: return false
        val descripteur = car.getDescriptor(Protocole.CCCD) ?: return false
        if (!g.setCharacteristicNotification(car, true)) return false
        val attente = CompletableDeferred<Int>()
        attenteDescripteur = attente
        val valeur = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
        val lance = if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(descripteur, valeur) == BluetoothStatusCodes.SUCCESS
        } else {
            @Suppress("DEPRECATION")
            descripteur.value = valeur
            @Suppress("DEPRECATION")
            g.writeDescriptor(descripteur)
        }
        if (!lance) return false
        withTimeoutOrNull(30_000) { attente.await() } == BluetoothGatt.GATT_SUCCESS
    }

    private suspend fun ecrire(
        uuid: java.util.UUID,
        donnees: ByteArray,
        type: Int = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT,
    ): Boolean = operation.withLock {
        val g = gatt ?: return false
        val car = caracteristique(uuid) ?: return false
        val attente = CompletableDeferred<Int>()
        attenteEcriture = attente
        val lance = if (Build.VERSION.SDK_INT >= 33) {
            g.writeCharacteristic(car, donnees, type) == BluetoothStatusCodes.SUCCESS
        } else {
            car.writeType = type
            @Suppress("DEPRECATION")
            car.value = donnees
            @Suppress("DEPRECATION")
            g.writeCharacteristic(car)
        }
        if (!lance) return false
        withTimeoutOrNull(10_000) { attente.await() } == BluetoothGatt.GATT_SUCCESS
    }

    // ─────────────────────────────── Commandes ───────────────────────────────

    fun envoyerCommande(commande: String) {
        scope.launch {
            if (!ecrire(Protocole.COMMANDE, commande.toByteArray(Charsets.UTF_8))) {
                _reponses.tryEmit(ReponseCommande(commande, false, "Envoi impossible"))
            }
        }
    }

    fun effacerLogs() { _logs.value = emptyList() }

    // ─────────────────────────────── Mise à jour du firmware ───────────────────────────────

    /** Envoie un firmware (.bin produit par PlatformIO) au module. */
    fun mettreAJour(firmware: ByteArray) {
        if (_ota.value is EtatOta.EnCours) return
        scope.launch { executerMiseAJour(firmware) }
    }

    fun signalerErreurOta(message: String) { _ota.value = EtatOta.Erreur(message) }

    fun reinitialiserOta() { if (_ota.value !is EtatOta.EnCours) _ota.value = EtatOta.Inactif }

    private suspend fun executerMiseAJour(firmware: ByteArray) {
        fun erreur(message: String) { _ota.value = EtatOta.Erreur(message) }

        if (_etat.value != EtatConnexion.Connecte) return erreur("Module non connecté")
        if (firmware.size < 1024 || firmware[0] != Protocole.OCTET_MAGIQUE_FIRMWARE) {
            return erreur("Ce fichier n'est pas un firmware ESP32 (prendre firmware.bin dans .pio/build/…)")
        }

        _ota.value = EtatOta.EnCours(0f, "Préparation…")
        while (messagesOta.tryReceive().isSuccess) { /* on vide les anciens messages */ }

        val md5 = MessageDigest.getInstance("MD5").digest(firmware).joinToString("") { "%02x".format(it) }
        if (!ecrire(Protocole.OTA_CTRL, "START ${firmware.size} $md5".toByteArray())) return erreur("Envoi impossible")
        val debut = withTimeoutOrNull(15_000) { messagesOta.receive() }
        if (debut != "OK START") return erreur(debut ?: "Le module ne répond pas")

        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
        val tailleBloc = (mtu - 3).coerceIn(20, 509)
        var envoye = 0
        val depart = System.currentTimeMillis()
        while (envoye < firmware.size) {
            if (_etat.value != EtatConnexion.Connecte) return erreur("Connexion perdue pendant la mise à jour")
            val fin = minOf(envoye + tailleBloc, firmware.size)
            if (!ecrire(Protocole.OTA_DATA, firmware.copyOfRange(envoye, fin))) {
                ecrire(Protocole.OTA_CTRL, "ABORT".toByteArray())
                return erreur("Échec d'envoi à ${envoye / 1024} Ko")
            }
            messagesOta.tryReceive().getOrNull()?.let { if (it.startsWith("ERR")) return erreur(it) }
            envoye = fin
            val secondes = (System.currentTimeMillis() - depart) / 1000.0
            val debit = if (secondes > 0) envoye / 1024.0 / secondes else 0.0
            _ota.value = EtatOta.EnCours(
                envoye.toFloat() / firmware.size,
                "%d / %d Ko (%.1f Ko/s)".format(envoye / 1024, firmware.size / 1024, debit),
            )
        }

        _ota.value = EtatOta.EnCours(1f, "Vérification…")
        if (!ecrire(Protocole.OTA_CTRL, "END".toByteArray())) return erreur("Envoi impossible")
        val finOta = withTimeoutOrNull(20_000) { messagesOta.receive() }
        if (finOta != "OK END") return erreur(finOta ?: "Le module ne répond pas")
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_BALANCED)
        _ota.value = EtatOta.Termine
    }

    // ─────────────────────────────── Callbacks Android ───────────────────────────────

    private fun recevoir(uuid: java.util.UUID, valeur: ByteArray) {
        val texte = valeur.toString(Charsets.UTF_8)
        when (uuid) {
            Protocole.TELEMETRIE -> _telemetrie.update { Protocole.appliquer(it, texte) }
            Protocole.REPONSE -> Protocole.lireReponse(texte)?.let { _reponses.tryEmit(it) }
            Protocole.LOGS -> _logs.update { (it + "${formatHeure.format(Date())}  ${texte.trimEnd()}").takeLast(500) }
            Protocole.OTA_CTRL -> messagesOta.trySend(texte.trim())
        }
    }

    private val rappelGatt = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g != gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                preparation = scope.launch { preparer(g) }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                val enMiseAJour = _ota.value is EtatOta.EnCours
                val finOtaReussie = _ota.value == EtatOta.Termine
                g.close()
                gatt = null
                preparation?.cancel()
                annulerAttentes()
                if (enMiseAJour) _ota.value = EtatOta.Erreur("Connexion perdue pendant la mise à jour")
                _etat.value = when {
                    finOtaReussie -> EtatConnexion.Deconnecte   // le module redémarre sur le nouveau firmware
                    _etat.value is EtatConnexion.Erreur -> _etat.value
                    status != BluetoothGatt.GATT_SUCCESS -> EtatConnexion.Erreur("Connexion perdue (code $status)")
                    else -> EtatConnexion.Deconnecte
                }
            }
        }

        override fun onMtuChanged(g: BluetoothGatt, nouveauMtu: Int, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) mtu = nouveauMtu
            attenteMtu?.complete(status)
        }

        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            attenteServices?.complete(status)
        }

        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) {
            attenteEcriture?.complete(status)
        }

        override fun onDescriptorWrite(g: BluetoothGatt, d: BluetoothGattDescriptor, status: Int) {
            attenteDescripteur?.complete(status)
        }

        // Android 13 et plus
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) {
            recevoir(c.uuid, value)
        }

        // Android 12 et moins (n'est plus appelé quand la version ci-dessus est redéfinie)
        @Deprecated("Remplacé par la version avec value sur Android 13")
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION")
            recevoir(c.uuid, c.value ?: return)
        }
    }

    fun liberer() {
        arreterRecherche()
        fermer()
    }
}
