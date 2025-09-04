#! /usr/bin/perl
my $ok="n\n";
my $smtp_serveur='smtp.gmail.com:587';
my $utilisateur='toto@gmail.com';
my $password='mot_de_passe';
my $destinataires='titi@free.fr,lili@orange.fr';
my $fichier='config_mail.txt';
my $rep="\n";
if (-e -f -r $fichier){
		if (open(F4, "<$fichier")){		
			#copie key/value depuis le fichier 'config_mail' dans hash.
			while (<F4>) {
				chomp;
				($k, $v) = split(/=/);
				$h{$k} = $v;
			}
			close(F4); 
			$utilisateur=$h{'utilisateur'}."\n";
			$password=$h{'password'}."\n";
			$smtp_serveur=$h{'smtp_serveur'}."\n";
			$destinataires=$h{'destinataires'}."\n";
		}
		print "\nServeur smtp: $smtp_serveur"."Utilisateur: $utilisateur"."Passeword gmail: $password"."Destinataires: $destinataires";
		print 'Est-ce correct (o/n) ?';
		$ok=<STDIN>;
		#$ok=chomp($ok);
	}

while ($ok eq "n\n")  {
		print "\n Serveur (SMTP:port) : ";
		$rep=<STDIN>;
		if ($rep ne "\n") {$smtp_serveur=$rep;}
		print "Adresse mail utilisateur:  ";
		$rep=<STDIN>;
		if ($rep ne "\n") {$utilisateur=$rep;}
		print "Mot de passe mail: ";
		$rep=<STDIN>;
		if ($rep ne "\n") {$password=$rep;}
		print "Mail du Destinataires: ";
		$rep=<STDIN>;
		if ($rep ne "\n") {$destinataires=$rep;}
		
		if (open(F1, ">$fichier")){	
			print F1 "smtp_serveur=$smtp_serveur";
			print F1 "utilisateur=$utilisateur";
			print F1 "password=$password";
			print F1 "destinataires=$destinataires";
			close (F1);
		}
		if (open(F4, "<$fichier")){		
					#copie key/value depuis le fichier 'config_mail' dans hash.
					while (<F4>) {
						chomp;
						($k, $v) = split(/=/);
						$h{$k} = $v;
					}
					close(F4); 
					$utilisateur=$h{'utilisateur'};
					$password=$h{'password'};
					$smtp_serveur=$h{'smtp_serveur'};
					$destinataires=$h{'destinataires'};
					
				}
		print "\nServeur smtp: $smtp_serveur\nUtilisateur: $utilisateur\nPasseword gmail: $password \nDestinataires: $destinataires\n";
		print 'Est-ce correct (o/n) ?';
		$ok=<STDIN>;
		#$ok=chomp($ok);
}
print "\n Pour lancer l'application dans un terminal entrer:";
print "./scan406.pl 406M 406.1M"  ;
