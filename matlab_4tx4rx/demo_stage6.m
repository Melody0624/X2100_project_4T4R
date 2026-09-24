function run=demo_stage6(showPlots)
%DEMO_STAGE6 Unified pipeline and point-cloud plot; export separately.
if nargin<1, showPlots=true; end
cfg=config_4tx4rx();
t(1)=struct('rangeM',12,'velocityMps',-7,'azimuthDeg',-20,'amplitudeCounts',120);
t(2)=struct('rangeM',30,'velocityMps',18,'azimuthDeg',25,'amplitudeCounts',100);
run=run_4tx4rx(t,cfg);
points=run.frames{1}.pointCloud.points;
fprintf('Synthetic point cloud: %d points; processing %.3f s\n',numel(points),run.frames{1}.elapsedS);
disp(struct2table(points));
if showPlots
    figure('Name','Stage 6 - synthetic point cloud','Position',[100 100 1050 480]);
    tiledlayout(1,2);
    nexttile;
    if ~isempty(points)
        scatter([points.radarYM],[points.radarXM],65,[points.radialVelocityMps],'filled');
    end
    hold on;
    for target=t, plot(target.rangeM*sind(target.azimuthDeg),target.rangeM*cosd(target.azimuthDeg),'kx','MarkerSize',12); end
    axis equal; xlim([-25 25]); ylim([0 40]); grid on; colorbar; clim([-30 30]);
    xlabel('Radar y (m)'); ylabel('Radar x forward (m)'); title('Radar coordinates; color = radial velocity (m/s)');
    nexttile;
    if ~isempty(points)
        scatter([points.worldYM],[points.worldXM],65,[points.radialVelocityMps],'filled');
    end
    axis equal; grid on; colorbar; clim([-30 30]);
    xlabel('World y (m)'); ylabel('World x (m)'); title('After installation rotation and translation');
end
end
