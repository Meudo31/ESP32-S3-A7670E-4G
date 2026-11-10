package fr.meudo.motoesp32

import fr.meudo.motoesp32.ble.EtatAppel
import fr.meudo.motoesp32.ble.EtatDuModule
import fr.meudo.motoesp32.ble.Evenement
import fr.meudo.motoesp32.ble.Protocole
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

/** Vérifie la lecture des messages produits par le firmware (src/module_appli.cpp et les autres modules). */
class ProtocoleTest {

    private fun lire(texte: String) = JSONObject(texte)

    @Test
    fun lit_un_message_gps_et_remplit_l_historique() {
        val etat = Protocole.appliquer(EtatDuModule(),
            lire("""{"t":"gps","fix":1,"lat":43.604652,"lon":1.444209,"alt":146.0,"vit":87.5,"vmax":121.0,"cap":270,"sat":12,"s_gps":7,"s_gal":5,"hdop":0.9,"utc":1793600000}"""),
            maintenantMs = 1000)
        val gps = etat.gps!!
        assertTrue(gps.positionValide)
        assertEquals(43.604652, gps.latitude, 1e-6)
        assertEquals(12, gps.satellites)
        assertEquals(5, gps.satellitesGalileo)
        assertEquals("O", Protocole.cardinal(gps.cap))
        assertEquals(1, etat.historiqueGps.size)
    }

    @Test
    fun position_invalide_ne_remplit_pas_l_historique() {
        val etat = Protocole.appliquer(EtatDuModule(), lire("""{"t":"gps","fix":0}"""))
        assertFalse(etat.gps!!.positionValide)
        assertTrue(etat.historiqueGps.isEmpty())
    }

    @Test
    fun lit_la_batterie_et_l_animation() {
        var etat = Protocole.appliquer(EtatDuModule(), lire("""{"t":"bat","ok":1,"v":3.95,"pct":80.5,"charge":1,"source":"USB ou 12 V"}"""))
        etat = Protocole.appliquer(etat, lire("""{"t":"neo","num":3,"nom":"Comète","lum":80,"allumee":1,"noms":["a","b","c","Comète"]}"""))
        assertEquals(80.5, etat.batterie!!.pourcentage, 1e-9)
        assertTrue(etat.batterie!!.enCharge)
        assertEquals("Comète", etat.neopixels!!.nom)
        assertEquals(4, etat.neopixels!!.noms.size)
    }

    @Test
    fun les_taches_arrivent_en_plusieurs_messages() {
        var etat = Protocole.appliquer(EtatDuModule(), lire("""{"t":"taches","debut":1,"l":[["gps",4096,1200,2,1]]}"""))
        etat = Protocole.appliquer(etat, lire("""{"t":"taches","l":[["sms",4096,900,2,1]]}"""))
        assertEquals(listOf("gps", "sms"), etat.taches.map { it.nom })
        etat = Protocole.appliquer(etat, lire("""{"t":"taches","debut":1,"l":[]}"""))
        assertTrue(etat.taches.isEmpty())
    }

    @Test
    fun lit_l_etat_d_un_appel() {
        val etat = Protocole.appliquer(EtatDuModule(), lire("""{"t":"appel","etat":1,"num":"+33612345678"}"""))
        assertEquals(EtatAppel.Etat.SONNERIE, etat.appel!!.etat)
    }

    @Test
    fun garde_l_etat_si_message_inconnu() {
        val avant = Protocole.appliquer(EtatDuModule(), lire("""{"t":"bat","ok":1,"v":3.95,"pct":80.5}"""))
        assertSame(avant, Protocole.appliquer(avant, lire("""{"t":"inconnu"}""")))
        assertNull(Protocole.lireJson("pas du json"))
    }

    @Test
    fun lit_les_evenements() {
        val r = Protocole.lireEvenement(lire("""{"t":"rep","cmd":"PING","ok":true,"msg":"pong"}""")) as Evenement.Reponse
        assertEquals("PING", r.commande)
        assertTrue(r.ok)

        val sms = Protocole.lireEvenement(lire("""{"t":"sms_recu","i":4,"de":"+33600000000","date":"25/11/03,10:12:00+04","txt":"Salut"}""")) as Evenement.SmsRecu
        assertEquals("Salut", sms.sms.texte)

        val page = Protocole.lireEvenement(lire("""{"t":"ls","chemin":"/photos","fin":1,"e":[["03-11-2026",1,0,0],["a.jpg",0,52000,1793600000]]}""")) as Evenement.PageDeDossier
        assertTrue(page.derniere)
        assertTrue(page.elements[0].estUnDossier)
        assertEquals(52000L, page.elements[1].taille)

        assertNull(Protocole.lireEvenement(lire("""{"t":"gps"}""")))
    }

    @Test
    fun chemins_tailles_et_durees() {
        assertEquals("/photos/a.jpg", Protocole.chemin("/photos", "a.jpg"))
        assertEquals("/a.jpg", Protocole.chemin("/", "a.jpg"))
        assertEquals("/", Protocole.parent("/photos"))
        assertEquals("/photos", Protocole.parent("/photos/a.jpg"))
        assertEquals("N", Protocole.cardinal(350.0))
        assertEquals("SO", Protocole.cardinal(225.0))
        assertEquals("2 min 05 s", Protocole.duree(125))
        assertEquals("1 j 01 h 00 min", Protocole.duree(90000))
    }

    @Test
    fun lit_les_reglages_de_la_camera() {
        val etat = Protocole.appliquer(EtatDuModule(),
            lire("""{"t":"camera","pret":1,"capteur":"OV2640","tmax":13,"r":{"taille":9,"qualite":12,"lum":-1,"miroir":1},"ns":["nettete","debruit"]}"""))
        val camera = etat.camera!!
        assertTrue(camera.prete)
        assertEquals("OV2640", camera.capteur)
        assertEquals(13, camera.tailleMaximum)
        assertEquals(9, camera.valeurs["taille"])
        assertEquals(-1, camera.valeurs["lum"])
        assertTrue("nettete" in camera.nonPrisEnCharge)
        assertFalse("lum" in camera.nonPrisEnCharge)
    }
}
