#!/usr/bin/env perl
# Script perl pour scanner la fréquence des balises 406MHz 
# Version adaptée pour dec406_v10.2

# Script perl inspiré  de  "rtlsdr_scan" des auteurs de "RS-master"
# https://github.com/rs1729

# ce script utilise "rtl_power" et "rtl_fm" pour la réception sur une clé SDR
# https://github.com/keenerd/rtl-sdr

# ainsi que "reset_dvbt"
# http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line

# le décodage se fait avec "dec406" de dec406_v10.2
# Auteur F4EHY 8-6-2020
# Adapté pour dec406_v10.2 - 2025

use POSIX qw(strftime);
use strict;
use warnings;
my $f1_scan = "406M"; 
my $f2_scan = "406.1M";
my $ppm = 0;
my $line;
my $utc;
my $date=0;
my $time=0;
my $f1=0;
my $f2=0;
my $step=0;
my @db;
my $i;
my $j;
my $frq=0;
my $dec = './dec406 --100 --M3';
my $dec1 = './dec406 --100 --M3 --osm'; 
my $filter = "lowpass 3000 highpass 10"; #highpass de 10Hz à 400Hz selon la qualité du signal

my $largeur = "12k";
my $WFM="";
my $squelch=-200;
my $snr = 6;
my $trouve;
my $mean;
my $powfile = sprintf 'log_power.csv';
my $smtp_serveur='smtp.gmail.com:587';
my $utilisateur='toto@gmail.com';
my $password='mot_de_passe_mail';
my $destinataires='dede@free.fr,lili@orange.fr';
lit_config_mail();
print "$smtp_serveur\n$utilisateur\n$password \n$destinataires\n";

my $var=@ARGV;
if ($var<2)
	{print "\n SYNTAXE:  scan406.pl  freq_Depart  freq_Fin [decalage_ppm [osm]]\n";
	print " Exemple:\n	scan406.pl 406M 406.1M\n	scan406.pl 406M 406.1M 0 osm\n\n";
	exit(0);
	}
	
for (my $i=0;$i<@ARGV;$i++)
{
	#print "\n $ARGV[$i]";
	if ($i==0)
	{$f1_scan=$ARGV[0];
	}
	if ($i==1)
	{$f2_scan=$ARGV[1];
	}
	if ($i==2)
	{$ppm=$ARGV[2];
	}
	if ($i==3)
	{  if ($ARGV[3] eq 'osm'){$dec=$dec1;}
	}
		
}


while (1) {
    reset_dvbt();
if ($f1_scan ne $f2_scan){
    my $freq_trouvee=0;
    while ($freq_trouvee==0){
	print "\n\nScan $f1_scan...$f2_scan ";
	$utc = strftime('Date: %d %m %Y   %Hh%Mm%Ss', gmtime);
	print " $utc UTC\n";
	print "...PATIENTER...\n";
	system("rtl_power -p $ppm -f $f1_scan:$f2_scan:400  -i55 -P -O -1 -e55 -w hamming $powfile 2>/dev/null");
	if ( $? == -1 ) {print "Erreur: $!\n";}
	my $fh;
	open ($fh, '<', "$powfile") or die "Erreur fichier: $!\n";
	my $num_lines = 0;
	$frq=0;
	my $Maxi=-200;
	while ($line = <$fh>) {
	    $num_lines += 1;
	    chomp $line;
	    @db = split(",", $line);
	    $date = $db[0];
	    shift @db;
	    $time = $db[0];
	    shift @db;
	    $f1 = $db[0];
	    shift @db;
	    $f2 = $db[0];
	    shift @db;
	    $step = $db[0];
	    shift @db;
	    shift @db;
	    
	    my $sum = eval join '+', @db;
	    $mean = $sum / scalar(@db);
	    $squelch = $mean + $snr;

	    for ($j = 0; $j < scalar(@db)-1; $j++) {
		if ($db[$j] > $Maxi) {
		    $Maxi=$db[$j];
		    $frq = $f1 + ($j)*$step;
		}
	    
	    }
	}
	#print "Date: $date   $time  $f1  $f2 $step \n";
	close $fh;
	
	if ($num_lines == 0) {
	    print "[reset dvb-t ...]\n";
	    reset_dvbt();
	}
	if ($Maxi>$squelch) {
	    $freq_trouvee=1;
	    my $f=$frq/1000000.0;
	    printf "\nFréquence: %.3fMHz Niveau: %.1fdB Moyenne: %.1fdB \n",$f,$Maxi,$mean;
	    }
    
    }  
}
else {
	$frq=1000000.0*substr($f1_scan, 0, -1);
	#print "\n $frq";
}
    do{
    print "\n\nLancement du Decodage   ";
    $utc = strftime(' %d %m %Y   %Hh%Mm%Ss', gmtime);
    print " UTC $utc";
    system("timeout 56s rtl_fm -p $ppm -M fm $WFM -s $largeur -f $frq  2>/dev/null |\ 
	    sox -t raw -r $largeur -e s -b 16 -c 1 - -t wav - $filter 2>/dev/null |\ 
	    $dec 1>./trame 2>./code ");  
      
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
    if($trouve eq 'TROUVE'){
        my $f_actuelle = $frq/1000000.0;
        if (freq_balise_autorisee($f_actuelle)) {
            envoi_mail();
            printf "✉️  Email envoyé pour %.3f MHz (canal autorisé)\n", $f_actuelle;
        } else {
            printf "🔇 Email supprimé pour %.3f MHz (canal filtré selon T.012)\n", $f_actuelle;
        }
    }

    } while ($trouve eq 'TROUVE');

}

# Frequency filtering function according to COSPAS-SARSAT T.012 Table H.2
# Returns 1 if email authorized, 0 if email filtered
sub freq_balise_autorisee {
    my $freq_mhz = shift;
    
    # Authorized channels (email sent) - according to Table H.2 October 2024
    my @canaux_autorises = (
        # Active channels with distress beacons
        [406.025, 406.025],  # Channel B - Beacons TA < 01/01/2002
        [406.028, 406.028],  # Channel C - Beacons TA < 01/01/2007
        [406.031, 406.031],  # Channel D - Beacons TA < 01/07/2025
        [406.037, 406.037],  # Channel F - Beacons TA < 01/01/2012
        [406.040, 406.040],  # Channel G - Beacons TA < 01/01/2017
        
        # Future channels for developments (TBD)
        [406.049, 406.049],  # Channel J - Future developments
        [406.052, 406.052],  # Channel K - Future developments
        [406.061, 406.061],  # Channel N - Future developments
        [406.064, 406.064],  # Channel O - Future developments
        [406.073, 406.073],  # Channel R - Future developments
        
        # New channel since 2025
        [406.076, 406.076],  # Channel S - Beacons TA > 01/01/2025
    );
    
    # Check if frequency is in authorized channels
    foreach my $canal (@canaux_autorises) {
        if ($freq_mhz >= ($canal->[0] - 0.002) && $freq_mhz <= ($canal->[1] + 0.002)) {
            return 1;  # Email authorized
        }
    }
    
    # All other frequencies are filtered:
    # - Channel A (406.022): System/calibration beacons
    # - Channels E,H,I,L,M,P,Q: Reserved not assigned
    # - > 406.076 MHz: Above channel S maximum
    # - < 406.025 MHz: Below channel B minimum (except system)
    return 0;  # Email suppressed
}

## http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line
## usbreset.c -> reset_usb

sub reset_dvbt {
    my @devices = split("\n",`lsusb`);
    foreach my $line (@devices) {
        if ($line =~ /\w+\s(\d+)\s\w+\s(\d+):\sID\s([0-9a-f]+):([0-9a-f]+).+Realtek Semiconductor Corp\./) {
            if ($4 eq "2832"  ||  $4 eq "2838") {
                system("./reset_usb /dev/bus/usb/$1/$2");
            }
        }
    }
}


sub envoi_mail {
    my $a='"./trame"';
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
					if (open (F3, '<', './trame')) {
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