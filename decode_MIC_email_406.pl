#!/usr/bin/env perl
# Script perl pour décoder les balises 406MHz par l'entrée MICRO avec envoi automatique de MAIL
# sox dirige le flux audio de l'entrée micro vers mon logiciel de décodage "dec406_v7"
# Brancher l'entrée MICRO puis pour lancer le décodage entrer la commande:
# ./MIC_email_406.pl 
#                                                                           F4EHY 18-03-2021

use POSIX qw(strftime);
use strict;
use warnings;

my $line;
my $utc;
my $date=0;
my $time=0;

my @db;
my $i;
my $j;
my $frq=0;
#my $dec = './dec406 --100 --M3 --une_minute';
#my $dec1 = './dec406 --100 --M3 --une_minute --osm';
my $dec = './dec406 --100 --M3';
my $dec1 = './dec406 --100 --M3 --osm';
my $timeout_duration = 15; # Timeout optimisé: 15s pour tests (5s), 50s pour balises réelles
my $timeout_min = 30;      # Timeout minimum (balises test/urgence)
my $timeout_max = 120;     # Timeout maximum (balises faibles)
my $scan_count = 0;        # Compteur de scans sans détection 
#my $filter = "lowpass 3000 highpass 400"; #highpass de 10Hz à 400Hz selon la qualité du signal
my $filter = "lowpass 3000 highpass 10";

#my $largeur = "12k";
#my $WFM="";
#my $squelch=-200;
#my $snr = 6;
my $trouve;
#my $mean;
my $smtp_serveur='smtp.gmail.com:587';
my $utilisateur='toto@gmail.com';
my $password='mot_de_passe_mail';
my $destinataires='dede@free.fr,lili@orange.fr';
lit_config_mail();
print "$smtp_serveur\n$utilisateur\n$password \n$destinataires\n";

my $var=@ARGV;
print "\n SYNTAXE:  MIC_406.pl  [osm] [timeout]\n";
print " Exemple:\n	MIC_406.pl \n	MIC_406.pl osm\n	MIC_406.pl osm 60\n	MIC_406.pl timeout 90\n\n";
	
for (my $i=0;$i<@ARGV;$i++)
{
	#print "\n $i--> $ARGV[$i]";
	if ($ARGV[$i] eq 'osm'){
		$dec=$dec1;
	}
	# Détection paramètre timeout manuel
	elsif ($ARGV[$i] =~ /^\d+$/ && $ARGV[$i] >= $timeout_min && $ARGV[$i] <= $timeout_max) {
		$timeout_duration = $ARGV[$i];
		print "  [Timeout manuel défini: ${timeout_duration}s]\n";
	}
		
}


while (1) {
    
    do{
    print "\n\nLancement du Decodage   ";
    $utc = strftime(' %d %m %Y   %Hh%Mm%Ss', gmtime);
    print " UTC $utc";
    # Détection système audio (PipeWire/ALSA/PulseAudio)
    my $audio_cmd = "sox -d";
    if (system("pgrep -x pipewire >/dev/null 2>&1") == 0) {
        $audio_cmd = "sox -t pulseaudio default";  # PipeWire compatible
        print "  [PipeWire détecté]\n" if ($scan_count == 0);
    } elsif (system("pgrep -x pulseaudio >/dev/null 2>&1") == 0) {
        $audio_cmd = "sox -t pulseaudio default";  # PulseAudio
        print "  [PulseAudio détecté]\n" if ($scan_count == 0);
    }
    
    system("timeout ${timeout_duration}s  stdbuf -i0 -o0 -e0 $audio_cmd -t wav - $filter 2>/dev/null | stdbuf -i0 -o0 -e0 $dec 1>./trame.asc 2>./code ");  
      
    $trouve="PAS encore trouve";
    my $ligne;
    if (open (F2, '<', './code')) {
	while (defined ($ligne = <F2>)) {
	    chomp $ligne;
	    #print "\nligne : $ligne\n";
	    if ($ligne eq 'TROUVE') {$trouve='TROUVE';}
	    }
	close F2;
	}

    affiche_trame();
    
    # Gestion adaptative du timeout selon type de balise
    if($trouve eq 'TROUVE'){
        envoi_mail();
        $scan_count = 0;  # Reset compteur après détection
        # Détection du type de balise basée sur la fréquence de répétition
        if ($timeout_duration <= 20) {
            # Balise de test détectée (trames fréquentes) - rester en mode rapide
            $timeout_duration = 15;
            print "  [Mode test: timeout 15s]\n";
        } else {
            # Balise d'exercice/réelle détectée - passer en mode standard
            $timeout_duration = 55;
            print "  [Mode exercice/réel: timeout 55s]\n";
        }
    } else {
        $scan_count++;
        # Adaptation progressive: test → exercice → recherche approfondie
        if ($scan_count == 1) {
            $timeout_duration = 55;  # Passer en mode balise réelle après 1 échec
            print "  [Passage en mode balise réelle: ${timeout_duration}s]\n";
        } elsif ($scan_count >= 3) {
            $timeout_duration = ($timeout_duration < $timeout_max) ? $timeout_duration + 20 : $timeout_max;
            print "  [Recherche approfondie: ${timeout_duration}s après $scan_count scans sans détection]\n";
        }
        # Pause courte entre scans vides (éviter surcharge CPU)
        sleep 2;
    }

    } while ($trouve eq 'TROUVE');

}


sub envoi_mail {
    my $a='"./trame.asc"';
    my $u='"Alerte_Balise_406"';
    my $m='" Date et Heure (UTC) du decodage: '.$utc.'"';
    my $l='email.log';
    my $s='"'.$smtp_serveur.'"';;
    my $o='tls=yes';
    my $f='"'.$utilisateur.'"';
    my $xu='"'.$utilisateur.'"';
    my $xp='"'.$password.'"';
    my $t='"'.$destinataires.'"';
    
system("sendemail -l $l -f $f -u $u -t $t -s $s -o $o -xu $xu -xp $xp -m $m -a $a 2>/dev/null 1>/dev/null");
    
}


# affichage fichier ./trame
sub affiche_trame {
					my $ligne;
					if (open (F3, '<', './trame.asc')) {
						while (defined ($ligne = <F3>)) {
							#chomp $ligne;
							print "$ligne";
							}
						close F3;
					}
}

#lire config_mail.txt
sub lit_config_mail {
		my ($k, $v);
		my %h;
		if (open(F4, "<config_mail.txt")){		
			#copie key/value depuis le fichier 'config_mail' dans hash.
			while (<F4>) {
				chomp;
				($k, $v) = split(/=/);
				$h{$k} = $v;
			}
			close F4; 
			$utilisateur=$h{'utilisateur'};
			$password=$h{'password'};
			$smtp_serveur=$h{'smtp_serveur'};
			$destinataires=$h{'destinataires'};
		}
}
