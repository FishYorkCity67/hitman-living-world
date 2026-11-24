# Living World – KI-NPCs & Realismus für HITMAN World of Assassination

Eine ZHMModSDK-Mod, mit der du mit jedem NPC reden kannst – getippt oder per Mikrofon. Die NPCs antworten
in ihrer Rolle über eine **lokale KI** (Ollama, läuft auf deinem PC, kostenlos, offline) und sprechen die
Antwort mit deutscher Stimme aus. Bewaffnete NPCs **schießen nicht mehr bei jeder Kleinigkeit**, sondern
warnen und verhandeln. Opfer und Zeugen **reagieren realistisch auf Schüsse und Messerstiche**.

Die vorhandene CheatMenu-Mod bleibt unverändert und läuft parallel.

## Installieren

1. HITMAN beenden.
2. Doppelklick auf **`Living World installieren.cmd`**.
3. Spiel starten. Fertig.

Entfernen: `Living World entfernen.cmd` (Sprachmodelle und Einstellungen bleiben liegen).

Voraussetzungen (bei dir alle vorhanden): ZHMModSDK 4.1.1, Ollama mit `qwen2.5:3b`, das
Whisper-Sprachmodell `ggml-small-q5_1.bin` (liegt schon in `Retail\mods\LivingWorld\models`).

## Steuerung

| Taste | Funktion |
|---|---|
| **T** | Chatfeld öffnen – tippen, **Enter** sagt es, **Esc** bricht ab, **Tab** wechselt Flüstern/Normal/Rufen |
| **B** (gedrückt halten) | Push-to-Talk: ins Mikrofon sprechen, loslassen |
| **F8** | Einstellungsfenster (auch im SDK-Menü unter „LIVING WORLD“) |

Alle Tasten lassen sich im Einstellungsfenster ändern (z. B. Push-to-Talk auf eine Maus-Seitentaste).

## Was die Mod macht

**Reden mit NPCs**
- Wer dich hört, hängt von der Lautstärke ab: Flüstern ≈ 2,5 m, normal ≈ 7 m, Rufen ≈ 18 m. Beim
  Mikrofon wird die Lautstärke automatisch an deiner Stimme erkannt.
- Angesprochen wird, wen du ansiehst. Nennst du einen Namen („Sophie, …“), ist diese Person gemeint.
  Ein laufendes Gespräch geht mit derselben Person weiter.
- Alle in Hörweite hören mit und merken es sich. Wenn du rufst, reagieren auch Umstehende.
- Jeder NPC hat eine eigene Persönlichkeit, eigene Stimmlage und ein Gedächtnis (Vertrauen, Misstrauen, Angst).
- Die KI kennt die Lage: deine Verkleidung (und ob der NPC dich als Fremden in seiner Uniform erkennt),
  sichtbare Waffe, Sperrbereich, Alarmstufe, Ort, was der NPC gerade tut.
- Personal kann – wenn du es überzeugst, bestichst oder einschüchterst – verraten, wo eine Zielperson ist.
- NPCs sprechen dich von sich aus an, wenn du eine Weile neben ihnen stehst.
- Untertitel unten, Sprechblasen über den Köpfen, Stimmen räumlich (leiser mit Abstand, links/rechts).
- Während du tippst, läuft das Spiel in Zeitlupe (abschaltbar).

**Erst reden, dann schießen**
- Will eine Wache zum ersten Mal auf dich schießen, hält die Mod den Schuss zurück: Sie zielt, ruft
  „Keine Bewegung!“ und gibt dir ein paar Sekunden.
- Solange du redest (tippst, sprichst, auf eine Antwort wartest), warten auch die anderen Bewaffneten ab.
- Die KI entscheidet: weiter verhandeln, dich gehen lassen (sie bleibt dann eine Weile friedlich) oder schießen.
- Greifst du selbst an oder schießt, wird sofort zurückgeschossen – kein Freifahrtschein.

**Verwundungen (experimentell)**
- Die Trefferstelle wird aus deiner Zielrichtung bestimmt (Bein, Arm, Oberkörper, Kopf).
- Beinschuss: der NPC humpelt (stark gebremst) und stolpert immer wieder. Zwei Beinschüsse: er geht zu Boden.
- Bauch-/Brustschuss oder Messerstich: er bricht zusammen, kriecht von dir weg, stöhnt und fleht – und verblutet nach 45–120 s.
- „Realistische Trefferzonen“: Beine und Arme töten nicht mehr sofort, sonst gäbe es in HITMAN kaum Verwundete.
- Messerstich (mit einer Klinge in der Hand): nur noch 30 % Schaden – der NPC geht zu Boden, kriecht und verblutet.
  HITMANs eigene Nahkampf-Tötungsanimationen (Takedowns) laufen am Schaden vorbei und töten weiterhin sofort.

**Drohen, Entwaffnen, Ruf (experimentell)**
- Drohst du („Ich bin bewaffnet“, „Hände hoch“, „Keine Bewegung, sonst bist du tot“), geraten alle Zuhörer in Panik
  und betteln sofort um ihr Leben – ohne auf die KI zu warten. Die ausführliche KI-Antwort kommt danach.
- Unbewaffnete und Bewaffnete, die gegen deinen Ruf keine Chance sehen, **erstarren vor Angst**: Sie rennen nicht mehr
  wie im Originalspiel davon, um Alarm zu schlagen, sondern weichen höchstens langsam zurück (30 s, einstellbar).
- Armtreffer gegen einen Bewaffneten (oder ein Schlag ohne Waffe): mit 70 % Chance fällt ihm die Waffe aus der Hand.
  Er schießt nicht mehr, ergibt sich und bettelt.
- Ruf: Jeder getötete Wachmann/Polizist/Soldat spricht sich herum. Wachen zögern länger mit dem Schießen,
  ab 3 getöteten Bewaffneten ergeben sie sich bei Drohungen eher, und die KI-Gespräche wissen davon.

**Nur noch die Stimmen der Mod (experimentell)**
- Die Original-Stimmen der NPCs werden abgeschaltet, bevor das Spiel sie lädt. Greift das bei manchen NPCs
  nicht: in HITMAN unter Optionen → Audio die Dialog-Lautstärke senken (betrifft dann auch 47).
- Frauen bekommen Frauenstimmen (Katja oder Hedda, fest pro Person), Männer Stefan mit eigener Tonlage.

**Realistische Reaktionen auf Gewalt**
- Getroffene schreien je nach Art: „Ich bin getroffen!“, „Er hat ein Messer!“, „Lass mich los!“.
- Zeugen geraten in Panik („Er hat ihn erstochen!“) oder greifen als Wache ein („Messer fallen lassen!“).
  Wer nur einen Schuss hört, wird unruhig („Was war das?!“).
- Auch Tötungen und Würgegriffe direkt neben dir werden bemerkt.
- Alle merken sich, was sie erlebt haben: Sprichst du sie danach an, reagieren sie verletzt, verängstigt
  oder wütend – nicht so, als wäre nichts passiert.

## Einstellungsfenster (F8)

Allgemein · KI (Server, Modell, Download, Testgespräch) · Stimmen (Probe hören) · Spracheingabe
(Mikrofon, Whisper-Modell, Mikrofontest) · Verhalten (Hörweiten, Deeskalation, Gewalt-Reaktionen) ·
NPCs (Live-Liste in der Nähe) · Gespräche (Verlauf) · Diagnose (Protokoll).

## Tempo der KI (wichtig)

Gemessen auf diesem Laptop (RTX 4050 mit 6 GB):

| Situation | qwen2.5:3b |
|---|---|
| ohne Spiel (Grafikkarte) | ca. 1,5 s pro Antwort |
| während HITMAN läuft | ca. 10–14 s pro Antwort |

HITMAN belegt fast den ganzen Grafikspeicher, deshalb rechnet die KI im Spiel auf dem Prozessor.
Standard ist deshalb das kleinere **qwen3:1.7b** (1,4 GB). **qwen2.5:3b** spricht saubereres Deutsch, ist aber langsamer.
Am meisten bringt niedrigere **Texturqualität** in HITMAN – dann passt das Modell auf die Grafikkarte (~1 s pro Antwort).
(`qwen2.5:1.5b` hat sich im Test nicht bewährt und kann mit `ollama rm qwen2.5:1.5b` gelöscht werden.)

Bei Rucklern: unter KI „KI-Prozessorkerne“ auf 4 stellen.

Seit Version 3 der Einstellungen:
- **Schnelle Antworten**: Der NPC spricht, sobald der Satz fertig ist; Vertrauen, Angst usw. schreibt die KI danach.
- Reaktionen von Umstehenden, die zu lange warten mussten, werden verworfen, statt den Gesprächspartner aufzuhalten.
- Das Vorwärmen beim Missionsstart lädt den Regeltext schon in den Zwischenspeicher der KI.
- Im Protokoll steht pro Antwort, wo die Zeit geblieben ist (Warten, Laden, Prompt lesen, Schreiben).
- Großer Hebel im Spiel: **Bildrate in HITMAN begrenzen** (z. B. 60 FPS). Die KI teilt sich die Grafikkarte mit dem Spiel.

**Was die Mod am NPC-Verhalten NICHT ändern kann:** Laufwege, Suchen, Alarm und Kampfroutinen sind die
fest eingebaute KI von HITMAN. Das SDK bietet keine Funktion, NPCs eigene Befehle zu geben. Die Mod greift dort ein,
wo es geht: Schüsse zurückhalten, Tempo bremsen (Humpeln, Erstarren), zu Boden gehen lassen, Waffe aus der Hand,
eigene Stimmen und Sätze.

Spracherkennung: ca. 1 Sekunde pro Satz (Prozessor), sehr genau auf Deutsch.

## Was getestet ist – und was noch nicht

Getestet (außerhalb des Spiels, automatische Tests in `Mod\Tests`): KI-Antworten über Ollama inklusive
Konfrontation und Reaktion nach einem Schuss, deutsche Stimmen (Katja, Hedda, Stefan),
Spracherkennung Wort für Wort korrekt, Stille ergibt keinen Text, Deeskalations-Logik, Gesprächspartner-Wahl,
Einstellungen. Die DLL passt zu deinem installierten SDK (alle 21 benötigten Funktionen vorhanden).

**Noch nicht im laufenden Spiel getestet** – bitte ausprobieren und Rückmeldung geben:
- ob das Zurückhalten der NPC-Schüsse greift (Zähler im Diagnose-Tab: „Abgefangene NPC-Schüsse“),
- ob Treffer korrekt als Schuss/Stich erkannt werden (Zähler „Gewalt-Ereignisse“),
- wie die Namen/Berufe der NPCs im Spiel aussehen.

Bei Problemen: `Retail\mods\LivingWorld\LivingWorld.log` (wird bei jedem Start neu angelegt).

## Für Weiterentwicklung

- Quellcode: `Mod\Src\Core` (KI, Stimmen, Logik – ohne Spiel testbar), `Mod\Src\Plugin` (Spiel-Anbindung).
- Bauen: `powershell -ExecutionPolicy Bypass -File tools\build.ps1 -RunTests` (danach `-Install`).
- Der SDK-Baum liegt in `D:\HitmanModding\ZHMModSDK` (nicht mehr in `%TEMP%` – dort hatte Windows
  bereits Dateien gelöscht). Benötigt: VS Build Tools 2022, Rust (cargo) – beides installiert.
- Tests: `LivingWorldTests.exe` (offline), `--llm` (Ollama), `--tts`, `--stt <modell.bin>`.

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE). Die Mod baut auf dem [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) auf, das ebenfalls unter der GPL-3.0 steht.

Copyright (C) 2026 FishYorkCity67. Inoffizielle Fan-Mod, nicht mit IO Interactive verbunden.

Die fertige `LivingWorld.dll` liegt nicht im Repository. Wie man sie baut, steht oben unter „Für Weiterentwicklung“.
