function load_dsss_example()
% LOAD_DSSS_EXAMPLE - Charge l'exemple DSSS Receiver dans MATLAB
%
% Usage:
%   load_dsss_example()
%
% Cela configure le chemin MATLAB et ouvre l'exemple DSSS

% Chemin de base de l'exemple
basePath = fileparts(mfilename('fullpath'));
examplePath = fullfile(basePath, 'DSSSReceiverForSARbasedTrackingSystemExample', 'dsss-receiver-for-sar-based-tracking-system');

% Ajouter au path MATLAB
addpath(basePath);
addpath(examplePath);

fprintf('=== DSSS Receiver Example ===\n');
fprintf('Chemin ajouté : %s\n', examplePath);
fprintf('\nFichiers disponibles :\n');
fprintf('  - dsss_receiver.m           : Fonction principale du récepteur\n');
fprintf('  - helperDSSSTransmitter.m   : Générateur de signal DSSS\n');
fprintf('  - helperPolyphaseCorrelator.m : Corrélateur polyphase\n');
fprintf('  - test_dsss_receiver.m      : Script de test\n');
fprintf('\nPour lancer l''exemple, tapez :\n');
fprintf('  cd(''%s'')\n', examplePath);
fprintf('  edit dsss_receiver.m\n');
fprintf('\n');

% Change vers le répertoire de l'exemple
cd(examplePath);

% Liste les fichiers
ls('*.m');

end
