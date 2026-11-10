package fr.meudo.motoesp32.ble

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID

/**
 * Protocole BLE partagé avec le firmware (include/module_appli.h).
 * Voir docs/protocole_ble.md à la racine du dépôt.
 */
object Protocole {
    const val NOM_MODULE = "Moto_ESP32"

    val SERVICE: UUID = UUID.fromString("7a3b0001-5c1e-4b8a-9f2d-6d6f746f3031")
    val DONNEES: UUID = UUID.fromString("7a3b0002-5c1e-4b8a-9f2d-6d6f746f3031")
    val COMMANDE: UUID = UUID.fromString("7a3b0003-5c1e-4b8a-9f2d-6d6f746f3031")
    val REPONSE: UUID = UUID.fromString("7a3b0004-5c1e-4b8a-9f2d-6d6f746f3031")
    val JOURNAL: UUID = UUID.fromString("7a3b0005-5c1e-4b8a-9f2d-6d6f746f3031")
    val OTA_CTRL: UUID = UUID.fromString("7a3b0006-5c1e-4b8a-9f2d-6d6f746f3031")
    val OTA_DATA: UUID = UUID.fromString("7a3b0007-5c1e-4b8a-9f2d-6d6f746f3031")
    val FICHIER: UUID = UUID.fromString("7a3b0008-5c1e-4b8a-9f2d-6d6f746f3031")
    /** Fichier envoyé du téléphone vers la carte SD (écriture avec accusé de réception). */
    val FICHIER_ENTRANT: UUID = UUID.fromString("7a3b0009-5c1e-4b8a-9f2d-6d6f746f3031")

    /** Descripteur standard qui active les notifications. */
    val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    /** Premier octet de toute image de firmware ESP32. */
    const val OCTET_MAGIQUE_FIRMWARE: Byte = 0xE9.toByte()

    /** Nombre de points gardés dans les historiques (graphiques). */
    const val TAILLE_DES_HISTORIQUES = 300

    /** Lit un texte JSON, ou null s'il est invalide. */
    fun lireJson(texte: String): JSONObject? = try { JSONObject(texte) } catch (e: Exception) { null }

    /**
     * Applique un message d'état au modèle. Les messages inconnus (ou les événements) laissent l'état inchangé.
     * [maintenantMs] sert à horodater les historiques.
     */
    fun appliquer(etat: EtatDuModule, j: JSONObject, maintenantMs: Long = System.currentTimeMillis()): EtatDuModule =
        when (j.optString("t")) {
            "gps" -> {
                val gps = EtatGps(
                    positionValide = j.optInt("fix") == 1,
                    typeDePosition = j.optInt("type"),
                    latitude = j.optDouble("lat", 0.0),
                    longitude = j.optDouble("lon", 0.0),
                    altitude = j.optDouble("alt", 0.0),
                    vitesseKmh = j.optDouble("vit", 0.0),
                    vitesseMaxKmh = j.optDouble("vmax", 0.0),
                    cap = j.optDouble("cap", 0.0),
                    pdop = j.optDouble("pdop", 0.0),
                    hdop = j.optDouble("hdop", 0.0),
                    vdop = j.optDouble("vdop", 0.0),
                    satellitesGps = j.optInt("s_gps"),
                    satellitesGlonass = j.optInt("s_glo"),
                    satellitesGalileo = j.optInt("s_gal"),
                    satellitesBeidou = j.optInt("s_bds"),
                    satellites = j.optInt("sat"),
                    heureUtc = j.optLong("utc"),
                    ageSecondes = j.optInt("age", -1),
                )
                val historique = if (gps.positionValide) {
                    (etat.historiqueGps + PointGps(maintenantMs, gps.vitesseKmh, gps.altitude, gps.latitude, gps.longitude))
                        .takeLast(TAILLE_DES_HISTORIQUES)
                } else etat.historiqueGps
                etat.copy(gps = gps, historiqueGps = historique)
            }
            "bat" -> {
                val b = EtatBatterie(
                    jaugePresente = j.optInt("ok") == 1,
                    tension = j.optDouble("v", 0.0),
                    pourcentage = j.optDouble("pct", 0.0),
                    variationParHeure = j.optDouble("taux", 0.0),
                    enCharge = j.optInt("charge") == 1,
                    pleine = j.optInt("pleine") == 1,
                    alertes = j.optInt("alertes"),
                    tension12V = j.optDouble("v12", 0.0),
                    tensionUsb = j.optDouble("vusb", 0.0),
                    mesuresCablees = j.optInt("mesure") == 1,
                    source = j.optString("source"),
                    autonomieHeures = j.optDouble("autonomie", -1.0),
                    versionJauge = j.optInt("jauge"),
                    tensionVueParLeModem = j.optDouble("vmodem", 0.0),
                    temperaturePuce = j.optDouble("temp", 0.0),
                )
                val historique = if (b.jaugePresente) {
                    (etat.historiqueBatterie + PointBatterie(maintenantMs, b.tension, b.pourcentage)).takeLast(TAILLE_DES_HISTORIQUES)
                } else etat.historiqueBatterie
                etat.copy(batterie = b, historiqueBatterie = historique)
            }
            "4g" -> {
                val r = EtatReseau(
                    modemPret = j.optInt("pret") == 1,
                    inscription = j.optInt("inscr"),
                    operateur = j.optString("op"),
                    technologie = j.optString("techno"),
                    csq = j.optInt("csq"),
                    signalDbm = j.optInt("dbm"),
                    signalPct = j.optInt("sig"),
                    mccMnc = j.optString("mccmnc"),
                    zone = j.optString("tac"),
                    cellule = j.optString("cell"),
                    bande = j.optString("bande"),
                    canal = j.optInt("earfcn"),
                    rsrp = j.optDouble("rsrp", 0.0),
                    rsrq = j.optDouble("rsrq", 0.0),
                    rssi = j.optDouble("rssi", 0.0),
                    snr = j.optDouble("snr", 0.0),
                    adresseIp = j.optString("ip"),
                    pointDAcces = j.optString("apn"),
                )
                val historique = if (r.signalPct > 0) {
                    (etat.historiqueSignal + PointSignal(maintenantMs, r.signalPct, r.rsrp)).takeLast(TAILLE_DES_HISTORIQUES)
                } else etat.historiqueSignal
                etat.copy(reseau = r, historiqueSignal = historique)
            }
            "sim" -> etat.copy(sim = EtatSim(
                modele = j.optString("modele"),
                versionLogiciel = j.optString("logiciel"),
                imei = j.optString("imei"),
                voixDisponible = j.optInt("voix") == 1,
                etat = j.optString("etat"),
                iccid = j.optString("iccid"),
                imsi = j.optString("imsi"),
                numero = j.optString("num"),
                numeroDesAlertes = j.optString("alerte_num"),
                alertesActives = j.optInt("alertes") == 1,
                alerteDemarrage = j.optInt("al_dem", 1) == 1,
                alertePosition = j.optInt("al_pos", 1) == 1,
                alerteDeplacement = j.optInt("al_dep", 1) == 1,
            ))
            "sys" -> etat.copy(systeme = EtatSysteme(
                versionFirmware = j.optString("fw"),
                compilation = j.optString("build"),
                versionCarte = j.optString("carte"),
                puce = j.optString("puce"),
                revision = j.optInt("rev"),
                coeurs = j.optInt("coeurs"),
                frequenceMhz = j.optInt("mhz"),
                temperature = j.optDouble("temp", 0.0),
                tailleFlash = j.optLong("flash"),
                frequenceFlashMhz = j.optInt("flash_mhz"),
                tailleProgramme = j.optLong("prog"),
                placeLibrePourProgramme = j.optLong("prog_libre"),
                versionSdk = j.optString("sdk"),
                adresseMac = j.optString("mac"),
                partition = j.optString("part"),
                dureeFonctionnementS = j.optLong("up"),
                raisonRedemarrage = j.optString("reset"),
                nombreDeDemarrages = j.optLong("boots"),
            ))
            "ble" -> etat.copy(liaison = EtatLiaison(
                signalDbm = j.optInt("rssi"),
                intervalleMs = j.optDouble("int", 0.0),
                delaiDeCoupureS = j.optDouble("to", 0.0),
                nombreDeDeconnexions = j.optLong("deco"),
                historique = j.optJSONArray("hist")?.let { liste -> (0 until liste.length()).map { liste.optString(it) } } ?: emptyList(),
            ))
            "mem" -> etat.copy(memoires = EtatMemoires(
                ramTotale = j.optLong("ram_total"),
                ramLibre = j.optLong("ram_libre"),
                ramMinimum = j.optLong("ram_min"),
                ramPlusGrandBloc = j.optLong("ram_bloc"),
                psramTotale = j.optLong("psram_total"),
                psramLibre = j.optLong("psram_libre"),
                psramMinimum = j.optLong("psram_min"),
                psramPlusGrandBloc = j.optLong("psram_bloc"),
            ))
            "taches" -> {
                val lues = lireLesTaches(j.optJSONArray("l"))
                etat.copy(taches = if (j.optInt("debut") == 1) lues else etat.taches + lues)
            }
            "carte" -> etat.copy(carte = EtatCarte(
                version = j.optString("version"),
                methodeDeDetection = j.optString("methode"),
                camera = j.optString("camera"),
                carteSdPresente = j.optInt("sd") == 1,
                typeCarteSd = j.optString("sd_type"),
                tailleCarteSd = j.optLong("sd_total"),
                espaceUtiliseCarteSd = j.optLong("sd_util"),
                sourceDeLHeure = j.optString("heure"),
                volume = j.optInt("vol"),
            ))
            "neo" -> etat.copy(neopixels = EtatNeopixels(
                numero = j.optInt("num"),
                nom = j.optString("nom"),
                luminosite = j.optInt("lum"),
                allumee = j.optInt("allumee") == 1,
                noms = j.optJSONArray("noms")?.let { tableau -> List(tableau.length()) { tableau.optString(it) } } ?: emptyList(),
            ))
            "camera" -> etat.copy(camera = EtatCamera(
                prete = j.optInt("pret") == 1,
                capteur = j.optString("capteur"),
                tailleMaximum = j.optInt("tmax", 13),
                valeurs = j.optJSONObject("r")?.let { r -> r.keys().asSequence().associateWith { r.optInt(it) } } ?: emptyMap(),
                nonPrisEnCharge = j.optJSONArray("ns")?.let { tableau -> List(tableau.length()) { tableau.optString(it) }.toSet() } ?: emptySet(),
            ))
            "appel" -> etat.copy(appel = EtatAppel(
                etat = EtatAppel.Etat.entries.getOrElse(j.optInt("etat")) { EtatAppel.Etat.AUCUN },
                numero = j.optString("num"),
                dureeSecondes = j.optLong("duree"),
            ))
            "can" -> {
                val can = lireLeBusCan(j)
                val v = can.valeurs
                val historique = if (can.etat == "actif" && (v.regime != null || v.vitesse != null)) {
                    (etat.historiqueCan + PointCan(maintenantMs, v.regime ?: 0.0, v.vitesse ?: 0.0)).takeLast(TAILLE_DES_HISTORIQUES)
                } else etat.historiqueCan
                etat.copy(canBus = can, historiqueCan = historique)
            }
            "can_ids" -> {
                val lues = lireLesTramesCan(j.optJSONArray("l"))
                etat.copy(tramesCan = if (j.optInt("debut") == 1) lues else etat.tramesCan + lues)
            }
            else -> etat
        }

    private fun lireLeBusCan(j: JSONObject): EtatCan {
        val v = j.optJSONObject("v") ?: JSONObject()
        fun nombre(cle: String): Double? = if (v.has(cle)) v.optDouble(cle) else null
        fun etat(cle: String): Boolean? = if (v.has(cle)) v.optInt(cle) == 1 else null
        val codes = j.optJSONArray("dtc") ?: JSONArray()
        return EtatCan(
            etat = j.optString("etat"),
            vitesseKbits = j.optInt("kbps"),
            vitesseReglee = j.optInt("reglage"),
            tramesParSeconde = j.optInt("tps"),
            totalDesTrames = j.optLong("total"),
            erreurs = j.optLong("err"),
            enregistrement = j.optInt("enr") == 1,
            tramesPerdues = j.optLong("perdues"),
            brocheTx = j.optInt("tx"),
            brocheRx = j.optInt("rx"),
            ageSecondes = j.optLong("age", -1),
            nombreDIdentifiants = j.optInt("ids"),
            fichier = j.optString("fichier"),
            probleme = j.optString("pb"),
            codesDefaut = List(codes.length()) { i ->
                val c = codes.getJSONArray(i)
                CodeDefautCan(spn = c.optLong(0), fmi = c.optInt(1), occurrences = c.optInt(2), source = c.optInt(3))
            },
            valeurs = ValeursCan(
                regime = nombre("rpm"), vitesse = nombre("vit"), vitesseAvant = nombre("vav"), patinage = nombre("pat"),
                rapport = if (v.has("rap")) v.optString("rap") else null,
                papillon = nombre("pap"), couple = nombre("cpl"), temperatureMoteur = nombre("tmot"),
                carburant = nombre("carb"), autonomieKm = nombre("auto"), debitCarburant = nombre("debit"),
                consoInstantanee = nombre("conso"), consoMoyenne = nombre("cmoy"), tensionBatterie = nombre("bat"),
                temperatureExterieure = nombre("text"), odometre = nombre("odo"), trip = nombre("trip"),
                pressionAvant = nombre("pav"), pressionArriere = nombre("par"),
                temperaturePneuAvant = nombre("tpav"), temperaturePneuArriere = nombre("tpar"),
                frein = etat("frein"), embrayage = etat("embr"), regulateur = etat("regul"),
                coupeCircuitSurRun = etat("coupe"), bequille = etat("beq"), capteurDeChute = if (v.has("chute")) v.optInt("chute") else null,
                clignotantGauche = etat("clig"), clignotantDroit = etat("clid"), warning = etat("warn"),
                feuCroisement = etat("croi"), feuRoute = etat("route"),
                lampes = if (v.has("lamp")) v.optInt("lamp") else null,
                obd = v.optInt("obd") == 1,
            ),
        )
    }

    private fun lireLesTramesCan(tableau: JSONArray?): List<TrameCan> {
        if (tableau == null) return emptyList()
        return List(tableau.length()) { i ->
            val t = tableau.getJSONArray(i)
            TrameCan(
                identifiant = t.optString(0), etendue = t.optInt(1) == 1, longueur = t.optInt(2),
                compteur = t.optLong(3), donnees = t.optString(4), ageSecondes = t.optLong(5),
            )
        }
    }

    /** PGN et adresse source d'un identifiant J1939 (29 bits) écrit en hexadécimal. */
    fun pgnEtSource(identifiantHexa: String): Pair<Long, Int>? {
        val id = identifiantHexa.toLongOrNull(16) ?: return null
        val pf = (id shr 16) and 0xFF
        var pgn = (id shr 8) and 0x3FFFF
        if (pf < 240) pgn = pgn and 0x3FF00
        return pgn to (id and 0xFF).toInt()
    }

    private fun lireLesTaches(tableau: JSONArray?): List<TacheDuModule> {
        if (tableau == null) return emptyList()
        return List(tableau.length()) { i ->
            val t = tableau.getJSONArray(i)
            TacheDuModule(t.optString(0), t.optLong(1), t.optLong(2), t.optInt(3), t.optInt(4))
        }
    }

    /** Lit un événement ponctuel (SMS reçu, page de dossier, photo...), ou null si ce n'en est pas un. */
    fun lireEvenement(j: JSONObject): Evenement? = when (j.optString("t")) {
        "rep" -> Evenement.Reponse(j.optString("cmd"), j.optBoolean("ok"), j.optString("msg"))
        "sms_recu" -> Evenement.SmsRecu(Sms(j.optInt("i"), j.optString("de"), j.optString("date"), j.optString("txt"), false))
        "sms" -> Evenement.SmsDeLaListe(Sms(j.optInt("i"), j.optString("de"), j.optString("date"), j.optString("txt"), j.optInt("lu") == 1))
        "sms_liste_debut" -> Evenement.DebutDeLaListeDesSms
        "sms_envoi" -> Evenement.ResultatEnvoiSms(j.optString("num"), j.optBoolean("ok"))
        "photo" -> Evenement.Photo(j.optBoolean("ok"), j.optString("chemin"), j.optLong("taille"), j.optString("msg"))
        "ls" -> {
            val tableau = j.optJSONArray("e") ?: JSONArray()
            val elements = List(tableau.length()) { i ->
                val e = tableau.getJSONArray(i)
                ElementFichier(e.optString(0), e.optInt(1) == 1, e.optLong(2), e.optLong(3))
            }
            Evenement.PageDeDossier(j.optString("chemin"), elements, j.optInt("fin") == 1)
        }
        "dl_debut" -> Evenement.DebutTelechargement(j.optString("chemin"), j.optLong("taille"))
        "dl_fin" -> Evenement.FinTelechargement(j.optString("chemin"), j.optLong("taille"), j.optBoolean("ok"), j.optLong("crc"), j.optString("msg"))
        else -> null
    }

    /** Point cardinal (N, NE, E...) pour un cap en degrés. */
    fun cardinal(degres: Double): String {
        val points = arrayOf("N", "NE", "E", "SE", "S", "SO", "O", "NO")
        val index = Math.floorMod(((degres + 22.5) / 45.0).toInt(), 8)
        return points[index]
    }

    /** Durée lisible : 3 j 04 h 12 min, 4 h 05 min, 12 min 30 s. */
    fun duree(secondes: Long): String {
        val j = secondes / 86400
        val h = (secondes % 86400) / 3600
        val m = (secondes % 3600) / 60
        val s = secondes % 60
        return when {
            j > 0 -> "%d j %02d h %02d min".format(j, h, m)
            h > 0 -> "%d h %02d min".format(h, m)
            else -> "%d min %02d s".format(m, s)
        }
    }

    /** Taille lisible : 512 o, 12,3 Ko, 4,5 Mo, 29,7 Go. */
    fun taille(octets: Long): String = when {
        octets < 1024 -> "$octets o"
        octets < 1024L * 1024 -> "%.1f Ko".format(octets / 1024.0)
        octets < 1024L * 1024 * 1024 -> "%.1f Mo".format(octets / 1048576.0)
        else -> "%.1f Go".format(octets / 1073741824.0)
    }

    /** Assemble un chemin de la carte SD : ("/photos", "a.jpg") → "/photos/a.jpg". */
    fun chemin(dossier: String, nom: String): String = if (dossier.endsWith("/")) "$dossier$nom" else "$dossier/$nom"

    /** Dossier parent : "/photos/03-11-2026" → "/photos", "/photos" → "/". */
    fun parent(chemin: String): String = chemin.substringBeforeLast('/', "").ifEmpty { "/" }

    /** Qualité du GPS d'après le HDOP. */
    fun qualiteGps(hdop: Double): String = when {
        hdop <= 0.0 -> "inconnue"
        hdop < 1.0 -> "idéale"
        hdop < 2.0 -> "excellente"
        hdop < 5.0 -> "bonne"
        hdop < 10.0 -> "moyenne"
        else -> "mauvaise"
    }

    fun nomInscription(code: Int): String = when (code) {
        1 -> "Inscrit (réseau de l'opérateur)"
        5 -> "Inscrit (itinérance)"
        2 -> "Recherche du réseau…"
        3 -> "Inscription refusée"
        else -> "Non inscrit"
    }
}

// ─────────────────────────────── Modèle de l'état du module ───────────────────────────────

data class EtatGps(
    val positionValide: Boolean, val typeDePosition: Int, val latitude: Double, val longitude: Double,
    val altitude: Double, val vitesseKmh: Double, val vitesseMaxKmh: Double, val cap: Double,
    val pdop: Double, val hdop: Double, val vdop: Double,
    val satellitesGps: Int, val satellitesGlonass: Int, val satellitesGalileo: Int, val satellitesBeidou: Int,
    val satellites: Int, val heureUtc: Long, val ageSecondes: Int,
)

data class PointGps(val instantMs: Long, val vitesseKmh: Double, val altitude: Double, val latitude: Double, val longitude: Double)

data class EtatBatterie(
    val jaugePresente: Boolean, val tension: Double, val pourcentage: Double, val variationParHeure: Double,
    val enCharge: Boolean, val pleine: Boolean, val alertes: Int, val tension12V: Double, val tensionUsb: Double,
    val mesuresCablees: Boolean, val source: String, val autonomieHeures: Double, val versionJauge: Int,
    val tensionVueParLeModem: Double, val temperaturePuce: Double,
)

data class PointBatterie(val instantMs: Long, val tension: Double, val pourcentage: Double)

data class EtatReseau(
    val modemPret: Boolean, val inscription: Int, val operateur: String, val technologie: String,
    val csq: Int, val signalDbm: Int, val signalPct: Int, val mccMnc: String, val zone: String, val cellule: String,
    val bande: String, val canal: Int, val rsrp: Double, val rsrq: Double, val rssi: Double, val snr: Double,
    val adresseIp: String, val pointDAcces: String,
)

data class PointSignal(val instantMs: Long, val signalPct: Int, val rsrp: Double)

data class EtatSim(
    val modele: String, val versionLogiciel: String, val imei: String, val voixDisponible: Boolean,
    val etat: String, val iccid: String, val imsi: String, val numero: String,
    val numeroDesAlertes: String = "", val alertesActives: Boolean = false,
    val alerteDemarrage: Boolean = true, val alertePosition: Boolean = true, val alerteDeplacement: Boolean = true,
)

data class EtatSysteme(
    val versionFirmware: String, val compilation: String, val versionCarte: String, val puce: String,
    val revision: Int, val coeurs: Int, val frequenceMhz: Int, val temperature: Double,
    val tailleFlash: Long, val frequenceFlashMhz: Int, val tailleProgramme: Long, val placeLibrePourProgramme: Long,
    val versionSdk: String, val adresseMac: String, val partition: String, val dureeFonctionnementS: Long,
    val raisonRedemarrage: String, val nombreDeDemarrages: Long,
)

/** La liaison Bluetooth vue par le module : signal, paramètres et dernières coupures. */
data class EtatLiaison(
    val signalDbm: Int, val intervalleMs: Double, val delaiDeCoupureS: Double,
    val nombreDeDeconnexions: Long, val historique: List<String>,
)

data class EtatMemoires(
    val ramTotale: Long, val ramLibre: Long, val ramMinimum: Long, val ramPlusGrandBloc: Long,
    val psramTotale: Long, val psramLibre: Long, val psramMinimum: Long, val psramPlusGrandBloc: Long,
)

data class TacheDuModule(val nom: String, val tailleDeLaPile: Long, val pileRestante: Long, val priorite: Int, val coeur: Int)

data class EtatCarte(
    val version: String, val methodeDeDetection: String, val camera: String,
    val carteSdPresente: Boolean, val typeCarteSd: String, val tailleCarteSd: Long, val espaceUtiliseCarteSd: Long,
    val sourceDeLHeure: String, val volume: Int,
)

data class EtatNeopixels(val numero: Int, val nom: String, val luminosite: Int, val allumee: Boolean, val noms: List<String>)

/** Réglages de la caméra du module (message « camera », voir module_camera.cpp). */
data class EtatCamera(
    val prete: Boolean, val capteur: String, val tailleMaximum: Int,
    val valeurs: Map<String, Int>, val nonPrisEnCharge: Set<String>,
)

data class EtatAppel(val etat: Etat, val numero: String, val dureeSecondes: Long) {
    enum class Etat { AUCUN, SONNERIE, NUMEROTATION, EN_COMMUNICATION }
}

data class EtatDuModule(
    val gps: EtatGps? = null,
    val batterie: EtatBatterie? = null,
    val reseau: EtatReseau? = null,
    val sim: EtatSim? = null,
    val systeme: EtatSysteme? = null,
    val liaison: EtatLiaison? = null,
    val memoires: EtatMemoires? = null,
    val taches: List<TacheDuModule> = emptyList(),
    val carte: EtatCarte? = null,
    val neopixels: EtatNeopixels? = null,
    val appel: EtatAppel? = null,
    val camera: EtatCamera? = null,
    val historiqueGps: List<PointGps> = emptyList(),
    val historiqueBatterie: List<PointBatterie> = emptyList(),
    val historiqueSignal: List<PointSignal> = emptyList(),
    val canBus: EtatCan? = null,
    val tramesCan: List<TrameCan> = emptyList(),
    val historiqueCan: List<PointCan> = emptyList(),
)

// ─────────────────────────────── Bus CAN de la moto ───────────────────────────────

/** Valeurs décodées des trames de la moto ; null = pas (encore) vue sur le bus. */
data class ValeursCan(
    val regime: Double? = null, val vitesse: Double? = null, val vitesseAvant: Double? = null, val patinage: Double? = null,
    val rapport: String? = null, val papillon: Double? = null, val couple: Double? = null, val temperatureMoteur: Double? = null,
    val carburant: Double? = null, val autonomieKm: Double? = null, val debitCarburant: Double? = null,
    val consoInstantanee: Double? = null, val consoMoyenne: Double? = null, val tensionBatterie: Double? = null,
    val temperatureExterieure: Double? = null, val odometre: Double? = null, val trip: Double? = null,
    val pressionAvant: Double? = null, val pressionArriere: Double? = null,
    val temperaturePneuAvant: Double? = null, val temperaturePneuArriere: Double? = null,
    val frein: Boolean? = null, val embrayage: Boolean? = null, val regulateur: Boolean? = null,
    val coupeCircuitSurRun: Boolean? = null, val bequille: Boolean? = null, val capteurDeChute: Int? = null,
    val clignotantGauche: Boolean? = null, val clignotantDroit: Boolean? = null, val warning: Boolean? = null,
    val feuCroisement: Boolean? = null, val feuRoute: Boolean? = null, val lampes: Int? = null, val obd: Boolean = false,
)

data class CodeDefautCan(val spn: Long, val fmi: Int, val occurrences: Int, val source: Int)

data class EtatCan(
    val etat: String, val vitesseKbits: Int, val vitesseReglee: Int, val tramesParSeconde: Int, val totalDesTrames: Long,
    val erreurs: Long, val enregistrement: Boolean, val tramesPerdues: Long, val brocheTx: Int, val brocheRx: Int,
    val ageSecondes: Long, val nombreDIdentifiants: Int, val fichier: String, val probleme: String,
    val codesDefaut: List<CodeDefautCan>, val valeurs: ValeursCan,
)

data class TrameCan(
    val identifiant: String, val etendue: Boolean, val longueur: Int, val compteur: Long, val donnees: String, val ageSecondes: Long,
)

data class PointCan(val instantMs: Long, val regime: Double, val vitesse: Double)

// ─────────────────────────────── Événements ───────────────────────────────

data class Sms(val numeroDansLaSim: Int, val expediteur: String, val date: String, val texte: String, val lu: Boolean)

data class ElementFichier(val nom: String, val estUnDossier: Boolean, val taille: Long, val dateModification: Long)

sealed interface Evenement {
    data class Reponse(val commande: String, val ok: Boolean, val message: String) : Evenement
    data class SmsRecu(val sms: Sms) : Evenement
    data class SmsDeLaListe(val sms: Sms) : Evenement
    data object DebutDeLaListeDesSms : Evenement
    data class ResultatEnvoiSms(val numero: String, val ok: Boolean) : Evenement
    data class Photo(val ok: Boolean, val chemin: String, val taille: Long, val message: String) : Evenement
    data class PageDeDossier(val chemin: String, val elements: List<ElementFichier>, val derniere: Boolean) : Evenement
    data class DebutTelechargement(val chemin: String, val taille: Long) : Evenement
    data class FinTelechargement(val chemin: String, val taille: Long, val ok: Boolean, val crc: Long, val message: String) : Evenement
}

/** Réponse à une commande (affichée en bas de l'écran). */
data class ReponseCommande(val commande: String, val ok: Boolean, val message: String)
