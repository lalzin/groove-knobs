# Prompt — Section « Plugins » de lalzin.com (téléchargement par email + don PayPal)

> À coller tel quel dans une session Claude Code ouverte dans le repo du site (`lalzin/`).
> Les visuels et les données viennent de `../groove-knobs/web-assets/`.

---

## Contexte

Tu travailles sur le site officiel de LALZIN (DJ / producteur funky house & disco) :
Next.js 14 App Router + TypeScript + Tailwind + Framer Motion, contenu FR/EN (`lib/i18n.ts`,
champs `L = { fr, en }`), Supabase côté serveur uniquement (`lib/supabase.ts`, clé
`service_role`, jamais exposée), déployé sur Vercel. DA funky-disco 70s : fond oxblood
`#1F0E1A`, magenta `#E8487E`, corail `#EE6C3A`, or `#E9AE3E`, teal `#1AA293`, crème `#F5E8CF`,
fonts Bricolage Grotesque (titres) + Hanken Grotesk (texte). Respecte l'existant
(composants, conventions, i18n, style), ne casse rien.

LALZIN publie **Groove Knobs**, une suite de plugins audio gratuits à un seul knob :
**French Filter**, **Boogie Trio**, **Backwash**, **Pew Pew** (un instrument MIDI) et
**Sing It Back** (effet pour voix) — VST3 / AU / standalone, macOS universel + Windows 64 bits. Il faut une section « Plugins » qui donne envie, et un téléchargement
contre une adresse email, avec un don libre via PayPal.

## Ressources fournies

1. Copie les visuels (WebP uniquement) et les données :
   ```bash
   mkdir -p public/images/plugins
   rsync -a --include='*/' --include='*.webp' --exclude='*' ../groove-knobs/web-assets/ public/images/plugins/
   cp ../groove-knobs/web-assets/plugins.json lib/plugins.json
   ```
   Par plugin (`public/images/plugins/<slug>/`, slugs `french-filter`, `boogie-trio`, `backwash`, `pew-pew`, `sing-it-back`) : `ui-hero.webp` (840×1040, fenêtre du plugin),
   `ui-hero@3x.webp` (1260×1560), `ui-<état>.webp` (autres états), `mascot.webp` (détouré),
   `poses/<n>.webp` (toutes les poses détourées), `og.webp` (1200×630), `card-square.webp`
   (1080×1080), `icon-512.webp`. Suite : `groove-knobs-og.webp`, `groove-knobs-banner.webp` (2400×1000).
2. `lib/plugins.json` : nom, slug, version, accroche et description FR/EN, points forts,
   presets, couleurs de chaque plugin, et pour chaque OS le nom du fichier d'installation,
   sa taille et la configuration requise. Type-le dans `lib/plugins.ts` et tout le contenu
   des pages vient de là (pas de texte en dur).

## Parcours (dans cet ordre)

1. **`/plugins`** — page de la suite : bannière `groove-knobs-banner`, accroche, 5 cartes
   (fenêtre du plugin + mascotte qui dépasse de la carte, couleurs du plugin, accroche,
   bouton « Télécharger gratuitement »). Ajoute « Plugins » dans la navigation et une
   section teaser sur la home. Ajoute aussi un lien dans `/links`.
2. **`/plugins/[slug]`** — page produit : hero (fenêtre + mascotte animée légèrement avec
   Framer Motion, flottement / rebond au survol ; respecte `prefers-reduced-motion`),
   description, points forts, liste des presets, galerie des états (`ui-*.webp`),
   configuration requise, formats, bouton de téléchargement. `generateStaticParams`, metadata
   par page avec `og.webp`, JSON-LD `SoftwareApplication` (Pew Pew : catégorie instrument, champ `type` de `plugins.json`) (`offers.price = 0`,
   `operatingSystem`, `applicationCategory: "MultimediaApplication"`).
3. **Formulaire de téléchargement** (sur la page produit, en modal ou en section ancrée) :
   - email (obligatoire), choix de l'OS (pré-sélectionné d'après le user-agent, modifiable),
     et possibilité de cocher les **autres plugins** (une seule demande, un seul email pour
     tous) ;
   - case **newsletter décochée par défaut** et séparée (« Je veux recevoir les nouveaux
     plugins et les sorties de LALZIN »), lien vers la politique de confidentialité ;
   - anti-spam : Cloudflare Turnstile + champ honeypot caché + limite de 5 demandes / heure
     par IP (hash) et de 3 / jour par email ;
   - à l'envoi : `POST /api/plugins/request` (route serveur, validation zod) → réponse
     immédiate → redirection vers `/plugins/merci?p=<slugs>`.
4. **Email via Resend** (expéditeur `LALZIN <downloads@lalzin.com>`, répondre à l'email de
   booking) : template React Email aux couleurs du site — visuel `og` du plugin, un bouton
   de téléchargement par plugin et par OS, mini guide d'installation (lien vers
   `/plugins/installation`), lien de don PayPal discret, signature « by LALZIN — with
   groove and love ». Version texte incluse. Objet : « Tes plugins Groove Knobs 🎛️ » /
   EN « Your Groove Knobs plugins 🎛️ » selon la langue du site au moment de la demande.
5. **`/plugins/merci`** — « Regarde ta boîte mail » + bouton « Je n'ai rien reçu » (renvoi
   possible après 60 s, mêmes limites) + bloc **don** : 3 boutons de montant suggéré
   (3 €, 5 €, 10 €) et « montant libre », qui ouvrent `https://paypal.me/<handle>/<montant>EUR`
   dans un nouvel onglet. Texte : les plugins sont gratuits, le don aide à en faire
   d'autres. Le don n'est jamais une condition du téléchargement.
6. **Liens de téléchargement** : `GET /api/plugins/download?t=<token>&p=<slug>&os=<macos|windows>`
   - le token est signé HMAC-SHA256 (même principe que `lib/auth.ts`, secret
     `DOWNLOAD_TOKEN_SECRET`), contient l'id de la demande et les plugins demandés,
     expire au bout de **7 jours** ;
   - si valide : incrémente le compteur, puis redirige (302) vers une **URL présignée de
     15 minutes** du fichier stocké en privé ; sinon page « Lien expiré » avec le
     formulaire pour en recevoir un nouveau.
7. **`/plugins/installation`** — guide FR/EN : macOS (ouvrir le `.pkg` : signé et notarisé par Apple, il s'installe sans avertissement), Windows
   (SmartScreen : « Informations complémentaires » → « Exécuter quand même »), puis dans
   Ableton : Réglages → Plug-Ins → Rescan, les plugins sont dans VST3 / Audio Units → LALZIN.
   Mentionne que Backwash démarre en mode LIVE (zéro latence) et écoute la première mesure,
   que **Pew Pew** est un instrument (piste MIDI, dans les instruments) et que **Sing It Back**
   se met sur une piste de voix seule (stem / a cappella, pas un mix complet) : régler d'abord
   le ton et la gamme du morceau, puis monter CROWD.

## Stockage et données

- **Fichiers d'installation** dans un bucket **Cloudflare R2 privé** (`groove-knobs`), clés
  `v0.1.0/FrenchFilter-0.1.0-macOS.pkg`, etc. (les cinq `.pkg` macOS de `groove-knobs/dist/`, signés et notarisés, et les cinq `.exe` Windows de la release GitHub v0.1.1) URLs présignées via `@aws-sdk/client-s3` +
  `@aws-sdk/s3-request-presigner` (endpoint R2). Rien dans `public/` (35 Mo par fichier).
- **Supabase**, nouvelle migration `supabase/plugins.sql` :
  - `plugin_requests` (id uuid, email citext, plugins text[], os, lang, newsletter bool,
    consent_at timestamptz null, ip_hash, user_agent, created_at) ;
  - `plugin_downloads` (id, request_id → plugin_requests, plugin, os, version, created_at) ;
  - RLS activée, aucune policy publique (accès uniquement par la clé `service_role`
    côté serveur).
- **Newsletter** : seulement si la case est cochée → ajout du contact dans une
  **Resend Audience** (`RESEND_AUDIENCE_ID`), avec l'horodatage du consentement.
- **Admin** : dans `/admin`, onglet « Plugins » en lecture : nombre de demandes, de
  téléchargements par plugin / OS / semaine, export CSV des emails **opt-in uniquement**.

## Variables d'environnement (à ajouter à `.env.example` et sur Vercel)

`RESEND_API_KEY`, `RESEND_AUDIENCE_ID`, `DOWNLOAD_TOKEN_SECRET`,
`R2_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY`, `R2_BUCKET`,
`NEXT_PUBLIC_TURNSTILE_SITE_KEY`, `TURNSTILE_SECRET_KEY`, `NEXT_PUBLIC_PAYPAL_ME`
(identifiant paypal.me uniquement), plus les variables Supabase existantes.

## RGPD

- Page `/confidentialite` (FR/EN) : données collectées (email, OS, choix newsletter, IP
  hachée), finalité (envoi des liens, statistiques), durée (24 mois), contact pour
  suppression, sous-traitants (Resend, Supabase, Cloudflare, Vercel).
- L'email de téléchargement est transactionnel ; aucun email marketing sans la case cochée.
  Lien de désinscription dans tout email de newsletter.

## Critères d'acceptation

- `npm run build` passe, aucune clé serveur exposée côté client (vérifie le bundle).
- Parcours complet en local avec de vraies clés de test : formulaire → email reçu → lien →
  téléchargement du bon fichier ; lien expiré → message clair ; limites anti-spam actives.
- Pages responsives (mobile d'abord), Lighthouse ≥ 90 en performance et accessibilité sur
  `/plugins` et une page produit, images en `next/image` avec tailles explicites.
- Toutes les chaînes en FR et EN, la langue de l'email suit la langue du site.
- Rien d'existant n'est cassé (home, `/links`, `/admin`).

À la fin, liste-moi ce que je dois faire à la main (DNS Resend, bucket R2, clés Turnstile,
upload des fichiers, variables Vercel), avec les étapes exactes.
