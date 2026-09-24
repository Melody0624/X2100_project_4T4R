function files=exportRun(run,outputStem)
%EXPORTRUN MAT preserves config/results; JSON contains compact point records.
% Explicitly requested output files are replaced if they already exist.
outputStem=char(outputStem);
folder=fileparts(outputStem);
if ~isempty(folder) && ~isfolder(folder), mkdir(folder); end
files.mat=[outputStem '.mat']; files.json=[outputStem '.json'];
save(files.mat,'run');
document=struct('synthetic',run.synthetic,'source',run.source, ...
    'waveformVerified',run.waveformVerified, ...
    'coordinateConvention','radar x forward, y positive azimuth; world = CCW rotation + translation', ...
    'velocityConvention','signed radial velocity only; not a 2D velocity vector', ...
    'installAngleDeg',run.cfg.pointCloud.installAngleDeg, ...
    'translationM',run.cfg.pointCloud.translationM,'frames',{run.frames});
fid=fopen(files.json,'w','n','UTF-8');
if fid<0, error('radar:export:Open','Cannot create JSON output.'); end
cleanup=onCleanup(@() fclose(fid)); %#ok<NASGU>
fprintf(fid,'%s',jsonencode(document,'PrettyPrint',true));
end
