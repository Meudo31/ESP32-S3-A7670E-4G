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
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.flow.first
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

private const val MORCEAUX_ENTRE_DEUX_CONFIRMATIONS = 8

/** Contenu d'un dossier de la carte SD, tel que reçu du module. */
data class ContenuDUnDossier(val chemin: String, val elements: List<ElementFichier>, val chargement: Boolean)

sealed interface EtatTelechargement {
    data object Inactif : EtatTelechargement
    data class EnCours(val chemin: String, val recus: Long, val total: Long) : EtatTelechargement
    data class Termine(val chemin: String, val contenu: ByteArray) : EtatTelechargement
    data class Erreur(val chemin: String, val message: String) : EtatTelechargement
}

private class ReceptionDeFichier(val chemin: String, val tailleAttendue: Long) {
    val contenu = java.io.ByteArrayOutputStream()
    val crc = java.util.zip.CRC32()
    var morceauxDepuisLaDerniereConfirmation = 0
}

/** Module trouvé pendant la recherche. */
/** Envoi d'un fichier du téléphone vers la carte SD du module. */
sealed interface EtatEnvoi {
    data object Inactif : EtatEnvoi
    data class EnCours(val chemin: String, val envoyes: Long, val total: Long) : EtatEnvoi
    data class Termine(val chemin: String) : EtatEnvoi
    data class Erreur(val chemin: String, val message: String) : EtatEnvoi
}

data class ModuleTrouve(val nom: String, val adresse: String, val rssi: Int)

sealed interface EtatConnexion {
    data object Deconnecte : EtatConnexion
    data object Recherche : EtatConnexion
    data class Connexion(val etape: String) : EtatConnexion
    /** Liaison perdue : l'application réessaie toute seule et garde les dernières données affichées. */
    data class Reconnexion(val tentative: Int) : EtatConnexion
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

    private val _etatModule = MutableStateFlow(EtatDuModule())
    /** Dernier état connu de chaque partie du module (GPS, batterie, réseau...). */
    val etatModule: StateFlow<EtatDuModule> = _etatModule.asStateFlow()

    private val _evenements = MutableSharedFlow<Evenement>(extraBufferCapacity = 64)
    /** Événements ponctuels : SMS reçu, photo prise, page de dossier... */
    val evenements: SharedFlow<Evenement> = _evenements.asSharedFlow()

    private val _sms = MutableStateFlow<List<Sms>>(emptyList())
    /** SMS stockés dans la carte SIM (après « Actualiser ») et SMS reçus depuis. */
    val sms: StateFlow<List<Sms>> = _sms.asStateFlow()

    private val _dossier = MutableStateFlow(ContenuDUnDossier("/", emptyList(), chargement = false))
    val dossier: StateFlow<ContenuDUnDossier> = _dossier.asStateFlow()

    private val _telechargement = MutableStateFlow<EtatTelechargement>(EtatTelechargement.Inactif)
    val telechargement: StateFlow<EtatTelechargement> = _telechargement.asStateFlow()

    private val _logs = MutableStateFlow<List<String>>(emptyList())
    val logs: StateFlow<List<String>> = _logs.asStateFlow()

    private val _envoi = MutableStateFlow<EtatEnvoi>(EtatEnvoi.Inactif)
    val envoi: StateFlow<EtatEnvoi> = _envoi.asStateFlow()
    @Volatile private var envoiAnnule = false

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

    // Reconnexion automatique : tant que l'utilisateur n'a pas appuyé sur « Déconnecter »,
    // une liaison perdue (module trop loin, éteint, redémarrage, mise à jour, Bluetooth du téléphone
    // coupé puis rallumé) est reprise toute seule, sans limite de temps.
    private var adresseDuModule: String? = null
    private var tentativesDeReconnexion = 0
    private var reconnexion: Job? = null

    /** Rallume la reconnexion quand le Bluetooth du téléphone est réactivé. */
    private val surveillanceDuBluetooth = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            if (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1) == BluetoothAdapter.STATE_ON
                && adresseDuModule != null && gatt == null
            ) {
                tentativesDeReconnexion = 0
                planifierLaReconnexion()
            }
        }
    }

    init {
        ContextCompat.registerReceiver(
            context, surveillanceDuBluetooth, IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )
    }
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
        arreterLaReconnexion()
        fermer()
        adresseDuModule = adresse
        _etat.value = EtatConnexion.Connexion("Connexion…")
        if (!ouvrirLaLiaison(adresse)) _etat.value = EtatConnexion.Erreur("Adresse invalide")
    }

    fun deconnecter() {
        adresseDuModule = null
        arreterLaReconnexion()
        fermer()
        _etat.value = EtatConnexion.Deconnecte
    }

    /**
     * [enAttente] = connexion « patiente » d'Android : elle attend que le module réapparaisse (hors de portée,
     * éteint, en train de redémarrer) sans délai maximum, au lieu d'abandonner au bout de 30 s.
     */
    private fun ouvrirLaLiaison(adresse: String, enAttente: Boolean = false): Boolean {
        val appareil = try { adaptateur?.getRemoteDevice(adresse) } catch (e: IllegalArgumentException) { null }
            ?: return false
        gatt = appareil.connectGatt(context, enAttente, rappelGatt, BluetoothDevice.TRANSPORT_LE)
        return true
    }

    /** Vrai si la liaison perdue doit être reprise toute seule (l'utilisateur n'a pas appuyé sur « Déconnecter »). */
    private fun reconnexionPossible(): Boolean = adresseDuModule != null

    /**
     * Relance la connexion après 1 s, 2 s, 4 s... puis toutes les 15 s, sans limite.
     * Chaque essai attend le module jusqu'à 60 s ; s'il ne répond pas, on ferme et on recommence
     * (certains téléphones restent bloqués sinon).
     */
    private fun planifierLaReconnexion() {
        val adresse = adresseDuModule ?: return
        reconnexion?.cancel()
        tentativesDeReconnexion++
        _etat.value = EtatConnexion.Reconnexion(tentativesDeReconnexion)
        reconnexion = scope.launch {
            delay(minOf(1_000L shl minOf(tentativesDeReconnexion - 1, 4), 15_000L))
            if (adresseDuModule != adresse || gatt != null) return@launch
            if (!bluetoothActif) return@launch   // la surveillance du Bluetooth relancera dès qu'il sera rallumé
            ouvrirLaLiaison(adresse, enAttente = true)
            delay(60_000)
            if (adresseDuModule == adresse && _etat.value is EtatConnexion.Reconnexion) {
                fermer()
                planifierLaReconnexion()
            }
        }
    }

    private fun arreterLaReconnexion() {
        reconnexion?.cancel()
        reconnexion = null
        tentativesDeReconnexion = 0
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
        attenteTelechargement?.complete(null)
        receptionEnCours = null
        if (_telechargement.value is EtatTelechargement.EnCours) {
            _telechargement.value = EtatTelechargement.Erreur("", "Connexion perdue")
        }
    }

    /** Étapes après la connexion : MTU, services, appairage par code PIN, notifications. */
    private suspend fun preparer(g: BluetoothGatt) {
        _etat.value = EtatConnexion.Connexion("Négociation…")
        attenteMtu = CompletableDeferred()
        if (g.requestMtu(517)) withTimeoutOrNull(5_000) { attenteMtu?.await() }

        _etat.value = EtatConnexion.Connexion("Lecture des services…")
        attenteServices = CompletableDeferred()
        // Android garde en mémoire la liste des caractéristiques d'un module appairé : on la vide pour voir
        // celles ajoutées par une mise à jour du firmware (ex. « Fichier entrant »).
        try { g.javaClass.getMethod("refresh").invoke(g) } catch (_: Exception) { }
        g.discoverServices()
        val statutServices = withTimeoutOrNull(10_000) { attenteServices?.await() }
        if (statutServices != BluetoothGatt.GATT_SUCCESS || g.getService(Protocole.SERVICE) == null) {
            echec("Ce module n'a pas le service de l'application (firmware à jour ?)", definitif = true)
            return
        }

        if (g.device.bondState != BluetoothDevice.BOND_BONDED) {
            _etat.value = EtatConnexion.Connexion("Appairage : saisissez le code PIN du module")
            if (!attendreAppairage(g.device)) {
                echec("Appairage refusé ou code PIN incorrect", definitif = true)
                return
            }
        }

        _etat.value = EtatConnexion.Connexion("Abonnement aux données…")
        for (uuid in listOf(Protocole.DONNEES, Protocole.REPONSE, Protocole.JOURNAL, Protocole.OTA_CTRL, Protocole.FICHIER)) {
            if (!activerNotifications(g, uuid)) {
                echec("Impossible de recevoir les données (appairage à refaire ?)")
                return
            }
        }
        _etat.value = EtatConnexion.Connecte
        reconnexion?.cancel()
        tentativesDeReconnexion = 0
        // Le téléphone donne l'heure au module (utile tant que le GPS et le réseau ne l'ont pas fait).
        envoyerCommande("HEURE ${System.currentTimeMillis() / 1000}")
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

    /** [definitif] : erreur qu'un nouvel essai ne réglera pas (code PIN refusé...) : on arrête de réessayer. */
    private fun echec(message: String, definitif: Boolean = false) {
        fermer()
        if (definitif) {
            adresseDuModule = null
            arreterLaReconnexion()
        }
        if (reconnexionPossible()) planifierLaReconnexion() else _etat.value = EtatConnexion.Erreur(message)
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
        // Pendant une reconnexion, les demandes automatiques des écrans sont ignorées sans message d'erreur.
        if (_etat.value is EtatConnexion.Reconnexion) return
        scope.launch {
            if (!ecrire(Protocole.COMMANDE, commande.toByteArray(Charsets.UTF_8))) {
                _reponses.tryEmit(ReponseCommande(commande, false, "Envoi impossible"))
            }
        }
    }

    fun effacerLogs() { _logs.value = emptyList() }

    /** Commande avec arguments séparés par « | » (ex. SMS_ENVOYER +336...|Bonjour). */
    fun envoyerCommande(nom: String, vararg arguments: String) {
        envoyerCommande(if (arguments.isEmpty()) nom else "$nom ${arguments.joinToString("|")}")
    }

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
        when (uuid) {
            Protocole.FICHIER -> recevoirUnMorceauDeFichier(valeur)
            Protocole.JOURNAL -> {
                val texte = valeur.toString(Charsets.UTF_8)
                _logs.update { (it + "${formatHeure.format(Date())}  ${texte.trimEnd()}").takeLast(500) }
            }
            Protocole.OTA_CTRL -> messagesOta.trySend(valeur.toString(Charsets.UTF_8).trim())
            Protocole.DONNEES, Protocole.REPONSE -> {
                val json = Protocole.lireJson(valeur.toString(Charsets.UTF_8)) ?: return
                _etatModule.update { Protocole.appliquer(it, json) }
                Protocole.lireEvenement(json)?.let { traiterUnEvenement(it) }
            }
        }
    }

    private fun traiterUnEvenement(evenement: Evenement) {
        when (evenement) {
            is Evenement.Reponse -> {
                if (evenement.commande != "DL_ACK") _reponses.tryEmit(ReponseCommande(evenement.commande, evenement.ok, evenement.message))
                if (evenement.commande == "FICHIERS" && !evenement.ok) _dossier.update { it.copy(chargement = false) }
            }
            Evenement.DebutDeLaListeDesSms -> _sms.value = emptyList()
            is Evenement.SmsDeLaListe -> _sms.update { liste -> (liste.filter { it.numeroDansLaSim != evenement.sms.numeroDansLaSim } + evenement.sms) }
            is Evenement.SmsRecu -> _sms.update { liste -> liste.filter { it.numeroDansLaSim != evenement.sms.numeroDansLaSim } + evenement.sms }
            is Evenement.PageDeDossier -> _dossier.update { actuel ->
                val dejaRecus = if (actuel.chemin == evenement.chemin && actuel.chargement) actuel.elements else emptyList()
                ContenuDUnDossier(
                    evenement.chemin,
                    (dejaRecus + evenement.elements).sortedWith(compareBy({ !it.estUnDossier }, { it.nom.lowercase() })),
                    chargement = !evenement.derniere,
                )
            }
            is Evenement.DebutTelechargement -> debuterLaReception(evenement)
            is Evenement.FinTelechargement -> terminerLaReception(evenement)
            else -> {}
        }
        _evenements.tryEmit(evenement)
    }

    // ─────────────────────────────── Carte SD : dossiers et téléchargements ───────────────────────────────

    fun ouvrirUnDossier(chemin: String) {
        _dossier.value = ContenuDUnDossier(chemin, emptyList(), chargement = true)
        envoyerCommande("FICHIERS $chemin")
    }

    fun actualiserLeDossier() = ouvrirUnDossier(_dossier.value.chemin)

    private var receptionEnCours: ReceptionDeFichier? = null
    private var attenteTelechargement: CompletableDeferred<ByteArray?>? = null

    /** Télécharge un fichier de la carte SD. Renvoie son contenu vérifié, ou null en cas d'échec. */
    suspend fun telecharger(chemin: String): ByteArray? {
        if (_telechargement.value is EtatTelechargement.EnCours) return null
        val attente = CompletableDeferred<ByteArray?>()
        attenteTelechargement = attente
        _telechargement.value = EtatTelechargement.EnCours(chemin, 0, 0)
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
        envoyerCommande("FICHIER_TELECHARGER $chemin")
        val resultat = withTimeoutOrNull(10 * 60_000) { attente.await() }
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_BALANCED)
        attenteTelechargement = null
        if (resultat == null && _telechargement.value is EtatTelechargement.EnCours) {
            _telechargement.value = EtatTelechargement.Erreur(chemin, "Le module ne répond pas")
        }
        return resultat
    }

    /**
     * Envoie [contenu] dans le fichier [chemin] de la carte SD (un fichier du même nom est remplacé).
     * Chaque morceau est écrit avec accusé de réception, puis le module vérifie la taille et le CRC32.
     */
    suspend fun envoyerUnFichier(chemin: String, contenu: ByteArray): Boolean {
        if (_envoi.value is EtatEnvoi.EnCours) return false
        envoiAnnule = false
        _envoi.value = EtatEnvoi.EnCours(chemin, 0, contenu.size.toLong())
        val debut = commandeAvecReponse("FICHIER_ENVOYER", "FICHIER_ENVOYER $chemin|${contenu.size}")
        if (debut == null || !debut.ok) {
            _envoi.value = EtatEnvoi.Erreur(chemin, debut?.message ?: "Le module ne répond pas")
            return false
        }
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH)
        // Une valeur Bluetooth fait au plus 512 octets (norme BLE) : avec un MTU de 517, MTU − 3 = 514 serait refusé
        // par le module. On prend donc au plus 509 octets, comme pour la mise à jour du firmware.
        val tailleDUnMorceau = (mtu - 3).coerceIn(20, 509)
        var position = 0
        var erreur: String? = null
        while (position < contenu.size) {
            if (envoiAnnule) { erreur = "Annulé"; break }
            val fin = minOf(position + tailleDUnMorceau, contenu.size)
            if (!ecrire(Protocole.FICHIER_ENTRANT, contenu.copyOfRange(position, fin))) {
                erreur = "Connexion perdue pendant l'envoi"
                break
            }
            position = fin
            _envoi.value = EtatEnvoi.EnCours(chemin, position.toLong(), contenu.size.toLong())
        }
        gatt?.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_BALANCED)
        if (erreur != null) {
            envoyerCommande("FICHIER_ENVOYER_ANNULER")
            _envoi.value = EtatEnvoi.Erreur(chemin, erreur)
            return false
        }
        val crc = java.util.zip.CRC32().apply { update(contenu) }.value
        val fin = commandeAvecReponse("FICHIER_ENVOYER_FIN", "FICHIER_ENVOYER_FIN $crc")
        _envoi.value = if (fin != null && fin.ok) EtatEnvoi.Termine(chemin)
                       else EtatEnvoi.Erreur(chemin, fin?.message ?: "Le module ne répond pas")
        if (fin?.ok == true) actualiserLeDossier()
        return fin?.ok == true
    }

    fun annulerLEnvoi() { envoiAnnule = true }

    fun signalerUneErreurDEnvoi(message: String) { _envoi.value = EtatEnvoi.Erreur("", message) }

    fun oublierLEnvoi() { if (_envoi.value !is EtatEnvoi.EnCours) _envoi.value = EtatEnvoi.Inactif }

    /** Envoie une commande et attend la réponse du module (null si rien au bout de 15 s). */
    private suspend fun commandeAvecReponse(nom: String, commande: String): ReponseCommande? = coroutineScope {
        val attente = async(start = CoroutineStart.UNDISPATCHED) {
            withTimeoutOrNull(15_000) { reponses.first { it.commande == nom } }
        }
        if (!ecrire(Protocole.COMMANDE, commande.toByteArray(Charsets.UTF_8))) {
            attente.cancel()
            return@coroutineScope null
        }
        attente.await()
    }

    fun annulerLeTelechargement() {
        envoyerCommande("DL_ANNULER")
        attenteTelechargement?.complete(null)
        receptionEnCours = null
        _telechargement.value = EtatTelechargement.Inactif
    }

    fun oublierLeTelechargement() {
        if (_telechargement.value !is EtatTelechargement.EnCours) _telechargement.value = EtatTelechargement.Inactif
    }

    private fun debuterLaReception(debut: Evenement.DebutTelechargement) {
        receptionEnCours = ReceptionDeFichier(debut.chemin, debut.taille)
        _telechargement.value = EtatTelechargement.EnCours(debut.chemin, 0, debut.taille)
    }

    private fun recevoirUnMorceauDeFichier(morceau: ByteArray) {
        val reception = receptionEnCours ?: return
        reception.contenu.write(morceau)
        reception.crc.update(morceau)
        reception.morceauxDepuisLaDerniereConfirmation++
        val recus = reception.contenu.size().toLong()
        // Le module attend une confirmation tous les 8 morceaux, et à la fin du fichier.
        if (reception.morceauxDepuisLaDerniereConfirmation >= MORCEAUX_ENTRE_DEUX_CONFIRMATIONS || recus >= reception.tailleAttendue) {
            reception.morceauxDepuisLaDerniereConfirmation = 0
            envoyerCommande("DL_ACK $recus")
        }
        _telechargement.value = EtatTelechargement.EnCours(reception.chemin, recus, reception.tailleAttendue)
    }

    private fun terminerLaReception(fin: Evenement.FinTelechargement) {
        val reception = receptionEnCours
        receptionEnCours = null
        val octets = reception?.contenu?.toByteArray()
        val verifie = fin.ok && octets != null && octets.size.toLong() == fin.taille && reception.crc.value == fin.crc
        if (verifie) {
            _telechargement.value = EtatTelechargement.Termine(fin.chemin, octets!!)
            attenteTelechargement?.complete(octets)
        } else {
            val message = if (!fin.ok) fin.message else "Fichier abîmé pendant le transfert (réessayer)"
            _telechargement.value = EtatTelechargement.Erreur(fin.chemin, message)
            attenteTelechargement?.complete(null)
        }
    }

    private val rappelGatt = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (g != gatt) return
            if (newState == BluetoothProfile.STATE_CONNECTED && status == BluetoothGatt.GATT_SUCCESS) {
                reconnexion?.cancel()   // l'essai a abouti : plus besoin du délai de 60 s
                preparation = scope.launch { preparer(g) }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED || status != BluetoothGatt.GATT_SUCCESS) {
                val enMiseAJour = _ota.value is EtatOta.EnCours
                val finOtaReussie = _ota.value == EtatOta.Termine
                g.close()
                gatt = null
                preparation?.cancel()
                annulerAttentes()
                if (enMiseAJour) _ota.value = EtatOta.Erreur("Connexion perdue pendant la mise à jour")
                // Après une mise à jour réussie, le module redémarre : on se reconnecte aussi au nouveau firmware.
                if (reconnexionPossible()) {
                    planifierLaReconnexion()
                    return
                }
                _etat.value = when {
                    finOtaReussie -> EtatConnexion.Deconnecte
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
        adresseDuModule = null
        arreterLaReconnexion()
        arreterRecherche()
        fermer()
        try { context.unregisterReceiver(surveillanceDuBluetooth) } catch (_: Exception) { }
    }
}
